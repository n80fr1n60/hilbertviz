#include "file_io.h"
#include "hilbert.h"
#include "image.h"
#include "palette.h"
#include "render.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void test_fail(const char *expr, const char *file, int line)
{
  (void)fprintf(stderr, "TEST FAIL: %s at %s:%d\n", expr, file, line);
  exit(1);
}

#define TEST_CHECK(expr) \
  do { \
    if (!(expr)) { \
      test_fail(#expr, __FILE__, __LINE__); \
    } \
  } while (0)

static void test_assert_file_content(const char *path, const uint8_t *expected, size_t expected_size);

static int test_write_all(int fd, const uint8_t *buf, size_t size)
{
  size_t total = 0u;

  while (total < size) {
    ssize_t n = write(fd, buf + total, size - total);
    if (n <= 0) {
      return 0;
    }
    total += (size_t)n;
  }

  return 1;
}

static void test_hilbert_order_helpers(void)
{
  uint32_t side = 0u;
  uint64_t capacity = 0u;

  TEST_CHECK(!hv_hilbert_side_for_order(0u, &side));
  TEST_CHECK(hv_hilbert_side_for_order(1u, &side));
  TEST_CHECK(side == 2u);

  TEST_CHECK(hv_hilbert_capacity_for_order(1u, &capacity));
  TEST_CHECK(capacity == 4u);

  TEST_CHECK(hv_hilbert_capacity_for_order(16u, &capacity));
  TEST_CHECK(capacity == (1ULL << 32u));
}

static void test_hilbert_pick_order(void)
{
  uint32_t order = 0u;
  uint32_t side = 0u;
  uint64_t capacity = 0u;

  TEST_CHECK(hv_hilbert_pick_order(0u, &order, &side, &capacity));
  TEST_CHECK(order == 1u);
  TEST_CHECK(side == 2u);
  TEST_CHECK(capacity == 4u);

  TEST_CHECK(hv_hilbert_pick_order(4u, &order, &side, &capacity));
  TEST_CHECK(order == 1u);

  TEST_CHECK(hv_hilbert_pick_order(5u, &order, &side, &capacity));
  TEST_CHECK(order == 2u);
  TEST_CHECK(side == 4u);
  TEST_CHECK(capacity == 16u);

  TEST_CHECK(!hv_hilbert_pick_order((1ULL << 32u) + 1u, &order, &side, &capacity));
}

static void test_hilbert_d2xy_order1(void)
{
  uint32_t x = 0u;
  uint32_t y = 0u;

  TEST_CHECK(hv_hilbert_d2xy(1u, 0u, &x, &y));
  TEST_CHECK((x == 0u) && (y == 0u));

  TEST_CHECK(hv_hilbert_d2xy(1u, 1u, &x, &y));
  TEST_CHECK((x == 0u) && (y == 1u));

  TEST_CHECK(hv_hilbert_d2xy(1u, 2u, &x, &y));
  TEST_CHECK((x == 1u) && (y == 1u));

  TEST_CHECK(hv_hilbert_d2xy(1u, 3u, &x, &y));
  TEST_CHECK((x == 1u) && (y == 0u));
}

static void test_hilbert_bijection(void)
{
  const uint32_t order = 4u;
  uint32_t side = 0u;
  uint64_t capacity = 0u;
  uint8_t *visited = 0;
  uint64_t d = 0u;

  TEST_CHECK(hv_hilbert_side_for_order(order, &side));
  TEST_CHECK(hv_hilbert_capacity_for_order(order, &capacity));

  visited = (uint8_t *)calloc((size_t)capacity, 1u);
  TEST_CHECK(visited != 0);

  for (d = 0u; d < capacity; ++d) {
    uint32_t x = 0u;
    uint32_t y = 0u;
    uint64_t idx = 0u;

    TEST_CHECK(hv_hilbert_d2xy(order, d, &x, &y));
    TEST_CHECK(x < side);
    TEST_CHECK(y < side);

    idx = ((uint64_t)y * (uint64_t)side) + (uint64_t)x;
    TEST_CHECK(idx < capacity);
    TEST_CHECK(visited[(size_t)idx] == 0u);
    visited[(size_t)idx] = 1u;
  }

  for (d = 0u; d < capacity; ++d) {
    TEST_CHECK(visited[(size_t)d] == 1u);
  }

  free(visited);
}

static void test_palette_edges(void)
{
  uint8_t rgb[3];

  hv_byte_to_rgb(0x00u, rgb);
  TEST_CHECK((rgb[0] == 0u) && (rgb[1] == 0u) && (rgb[2] == 0u));

  hv_byte_to_rgb(0x01u, rgb);
  TEST_CHECK((rgb[0] == 0u) && (rgb[1] == 32u) && (rgb[2] == 0u));

  hv_byte_to_rgb(0x1Fu, rgb);
  TEST_CHECK((rgb[0] == 0u) && (rgb[1] == 255u) && (rgb[2] == 0u));

  hv_byte_to_rgb(0x20u, rgb);
  TEST_CHECK((rgb[0] == 0u) && (rgb[1] == 0u) && (rgb[2] == 32u));

  hv_byte_to_rgb(0x7Eu, rgb);
  TEST_CHECK((rgb[0] == 0u) && (rgb[1] == 0u) && (rgb[2] == 255u));

  hv_byte_to_rgb(0x7Fu, rgb);
  TEST_CHECK((rgb[0] == 32u) && (rgb[1] == 0u) && (rgb[2] == 0u));

  hv_byte_to_rgb(0xFFu, rgb);
  TEST_CHECK((rgb[0] == 255u) && (rgb[1] == 0u) && (rgb[2] == 0u));
}

static void test_render_integration(void)
{
  char input_template[] = "/tmp/hv_input_XXXXXX";
  char output_template[] = "/tmp/hv_output_XXXXXX";
  uint8_t payload[] = {0x00u, 0x01u, 0x20u, 0x7Fu};
  int input_fd = -1;
  int output_fd = -1;
  HvRenderOptions options;
  HvRenderResult result;
  char err[256];
  FILE *out_file = 0;
  struct stat out_stat;
  char header[12];
  ssize_t wrote = 0;

  memset(&options, 0, sizeof(options));
  memset(&result, 0, sizeof(result));
  memset(err, 0, sizeof(err));

  input_fd = mkstemp(input_template);
  TEST_CHECK(input_fd >= 0);
  wrote = write(input_fd, payload, sizeof(payload));
  TEST_CHECK(wrote == (ssize_t)sizeof(payload));
  TEST_CHECK(close(input_fd) == 0);

  output_fd = mkstemp(output_template);
  TEST_CHECK(output_fd >= 0);
  TEST_CHECK(close(output_fd) == 0);

  options.input_path = input_template;
  options.output_path = output_template;
  options.auto_order = 1;

  TEST_CHECK(hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(result.order == 1u);
  TEST_CHECK(result.side == 2u);
  TEST_CHECK(result.input_bytes == sizeof(payload));
  TEST_CHECK(result.capacity == 4u);
  TEST_CHECK(result.page_count == 1u);

  TEST_CHECK(stat(output_template, &out_stat) == 0);
  TEST_CHECK((uint64_t)out_stat.st_size == 23u);

  out_file = fopen(output_template, "rb");
  TEST_CHECK(out_file != 0);
  TEST_CHECK(fread(header, 1u, 11u, out_file) == 11u);
  TEST_CHECK(memcmp(header, "P6\n2 2\n255\n", 11u) == 0);
  TEST_CHECK(fclose(out_file) == 0);

  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(output_template) == 0);
}

static void test_render_paginate_and_legend(void)
{
  char input_template[] = "/tmp/hv_page_input_XXXXXX";
  char output_template[] = "/tmp/hv_page_output_XXXXXX";
  char output_base[128];
  char legend_path[160];
  char page1[160];
  char page2[160];
  char page3[160];
  uint8_t payload[10];
  int input_fd = -1;
  int output_fd = -1;
  HvRenderOptions options;
  HvRenderResult result;
  char err[256];
  struct stat out_stat;
  FILE *legend_fp = 0;
  char legend_buf[1024];
  size_t legend_n = 0u;
  ssize_t wrote = 0;
  int i = 0;

  memset(&options, 0, sizeof(options));
  memset(&result, 0, sizeof(result));
  memset(err, 0, sizeof(err));

  for (i = 0; i < 10; ++i) {
    payload[i] = (uint8_t)i;
  }

  input_fd = mkstemp(input_template);
  TEST_CHECK(input_fd >= 0);
  wrote = write(input_fd, payload, sizeof(payload));
  TEST_CHECK(wrote == (ssize_t)sizeof(payload));
  TEST_CHECK(close(input_fd) == 0);

  output_fd = mkstemp(output_template);
  TEST_CHECK(output_fd >= 0);
  TEST_CHECK(close(output_fd) == 0);
  TEST_CHECK(unlink(output_template) == 0);

  TEST_CHECK(snprintf(output_base, sizeof(output_base), "%s.ppm", output_template) > 0);
  TEST_CHECK(snprintf(legend_path, sizeof(legend_path), "%s.legend.txt", output_template) > 0);
  TEST_CHECK(snprintf(page1, sizeof(page1), "%s_page0001.ppm", output_template) > 0);
  TEST_CHECK(snprintf(page2, sizeof(page2), "%s_page0002.ppm", output_template) > 0);
  TEST_CHECK(snprintf(page3, sizeof(page3), "%s_page0003.ppm", output_template) > 0);

  options.input_path = input_template;
  options.output_path = output_base;
  options.legend_path = legend_path;
  options.auto_order = 0;
  options.order = 1u;
  options.paginate = 1;
  options.legend_enabled = 1;

  TEST_CHECK(hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(result.order == 1u);
  TEST_CHECK(result.side == 2u);
  TEST_CHECK(result.capacity == 4u);
  TEST_CHECK(result.input_bytes == sizeof(payload));
  TEST_CHECK(result.page_count == 3u);

  TEST_CHECK(stat(page1, &out_stat) == 0);
  TEST_CHECK((uint64_t)out_stat.st_size == 23u);
  TEST_CHECK(stat(page2, &out_stat) == 0);
  TEST_CHECK((uint64_t)out_stat.st_size == 23u);
  TEST_CHECK(stat(page3, &out_stat) == 0);
  TEST_CHECK((uint64_t)out_stat.st_size == 23u);

  legend_fp = fopen(legend_path, "rb");
  TEST_CHECK(legend_fp != 0);
  legend_n = fread(legend_buf, 1u, sizeof(legend_buf) - 1u, legend_fp);
  TEST_CHECK(legend_n > 0u);
  legend_buf[legend_n] = '\0';
  TEST_CHECK(strstr(legend_buf, "total,10,1,9,0,0") != 0);
  TEST_CHECK(fclose(legend_fp) == 0);

  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(page1) == 0);
  TEST_CHECK(unlink(page2) == 0);
  TEST_CHECK(unlink(page3) == 0);
  TEST_CHECK(unlink(legend_path) == 0);
}

static void test_render_respects_max_image_cap(void)
{
  char input_template[] = "/tmp/hv_cap_input_XXXXXX";
  char output_template[] = "/tmp/hv_cap_output_XXXXXX";
  uint8_t payload[16];
  int input_fd = -1;
  int output_fd = -1;
  ssize_t wrote = 0;
  int i = 0;
  HvRenderOptions options;
  HvRenderResult result;
  char err[256];

  memset(&options, 0, sizeof(options));
  memset(&result, 0, sizeof(result));
  memset(err, 0, sizeof(err));

  for (i = 0; i < 16; ++i) {
    payload[i] = (uint8_t)i;
  }

  input_fd = mkstemp(input_template);
  TEST_CHECK(input_fd >= 0);
  wrote = write(input_fd, payload, sizeof(payload));
  TEST_CHECK(wrote == (ssize_t)sizeof(payload));
  TEST_CHECK(close(input_fd) == 0);

  output_fd = mkstemp(output_template);
  TEST_CHECK(output_fd >= 0);
  TEST_CHECK(close(output_fd) == 0);

  TEST_CHECK(setenv("HILBERTVIZ_MAX_IMAGE_BYTES", "32", 1) == 0);

  options.input_path = input_template;
  options.output_path = output_template;
  options.auto_order = 0;
  options.order = 2u; /* 16 pixels => 48-byte image buffer */

  TEST_CHECK(!hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(strstr(err, "exceeds configured cap") != 0);
  test_assert_file_content(input_template, payload, sizeof(payload));

  TEST_CHECK(unsetenv("HILBERTVIZ_MAX_IMAGE_BYTES") == 0);
  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(output_template) == 0);
}

static void test_file_io_slice_and_stream_semantics(void)
{
  char input_template[] = "/tmp/hv_fileio_input_XXXXXX";
  uint8_t payload[] = {0xA0u, 0xA1u, 0xA2u, 0xA3u, 0xA4u, 0xA5u, 0xA6u, 0xA7u};
  int input_fd = -1;
  ssize_t wrote = 0;
  HvBuffer slice;
  HvInputStream stream;
  uint8_t tmp[3];
  char err[256];

  memset(&slice, 0, sizeof(slice));
  memset(&stream, 0, sizeof(stream));
  memset(err, 0, sizeof(err));
  memset(tmp, 0, sizeof(tmp));

  input_fd = mkstemp(input_template);
  TEST_CHECK(input_fd >= 0);
  wrote = write(input_fd, payload, sizeof(payload));
  TEST_CHECK(wrote == (ssize_t)sizeof(payload));
  TEST_CHECK(close(input_fd) == 0);

  TEST_CHECK(hv_read_file_slice(input_template, 2u, 1, 4u, &slice, err, sizeof(err)));
  TEST_CHECK(slice.size == 4u);
  TEST_CHECK(memcmp(slice.data, payload + 2, 4u) == 0);
  hv_free_buffer(&slice);

  TEST_CHECK(!hv_read_file_slice(input_template, 99u, 0, 0u, &slice, err, sizeof(err)));
  TEST_CHECK(strstr(err, "offset") != 0);

  memset(err, 0, sizeof(err));
  TEST_CHECK(hv_open_file_slice_stream(input_template, 1u, 1, 3u, &stream, err, sizeof(err)));
  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(hv_stream_read_exact(&stream, tmp, sizeof(tmp), err, sizeof(err)));
  TEST_CHECK(memcmp(tmp, payload + 1, sizeof(tmp)) == 0);
  TEST_CHECK(hv_close_input_stream(&stream, err, sizeof(err)));
}

static void test_file_io_stream_detects_truncate_race(void)
{
  char input_template[] = "/tmp/hv_fileio_race_XXXXXX";
  uint8_t payload[] = {0xB0u, 0xB1u, 0xB2u, 0xB3u, 0xB4u, 0xB5u, 0xB6u, 0xB7u};
  uint8_t tmp[8];
  int input_fd = -1;
  ssize_t wrote = 0;
  HvInputStream stream;
  char err[256];

  memset(&stream, 0, sizeof(stream));
  memset(tmp, 0, sizeof(tmp));
  memset(err, 0, sizeof(err));

  input_fd = mkstemp(input_template);
  TEST_CHECK(input_fd >= 0);
  wrote = write(input_fd, payload, sizeof(payload));
  TEST_CHECK(wrote == (ssize_t)sizeof(payload));
  TEST_CHECK(close(input_fd) == 0);

  TEST_CHECK(hv_open_file_slice_stream(input_template, 0u, 1, 8u, &stream, err, sizeof(err)));
  TEST_CHECK(truncate(input_template, 2) == 0);
  TEST_CHECK(!hv_stream_read_exact(&stream, tmp, sizeof(tmp), err, sizeof(err)));
  TEST_CHECK(strstr(err, "unexpected EOF") != 0);
  TEST_CHECK(hv_close_input_stream(&stream, err, sizeof(err)));
  TEST_CHECK(unlink(input_template) == 0);
}

static void test_assert_file_content(const char *path, const uint8_t *expected, size_t expected_size)
{
  struct stat st;
  uint8_t *buf = 0;
  FILE *fp = 0;

  TEST_CHECK(stat(path, &st) == 0);
  TEST_CHECK((uint64_t)st.st_size == (uint64_t)expected_size);

  buf = (uint8_t *)malloc((expected_size > 0u) ? expected_size : 1u);
  TEST_CHECK(buf != 0);

  fp = fopen(path, "rb");
  TEST_CHECK(fp != 0);
  if (expected_size > 0u) {
    TEST_CHECK(fread(buf, 1u, expected_size, fp) == expected_size);
  }
  TEST_CHECK(fclose(fp) == 0);

  if (expected_size > 0u) {
    TEST_CHECK(memcmp(buf, expected, expected_size) == 0);
  }
  free(buf);
}

static void test_alias_legend_equals_input_rejected(void)
{
  char input_template[] = "/tmp/hv_alias_in_XXXXXX";
  char output_template[] = "/tmp/hv_alias_out_XXXXXX";
  uint8_t payload[] = {0x11u, 0x22u, 0x33u, 0x44u};
  int input_fd = -1;
  int output_fd = -1;
  ssize_t wrote = 0;
  HvRenderOptions options;
  HvRenderResult result;
  char err[256];

  memset(&options, 0, sizeof(options));
  memset(&result, 0, sizeof(result));
  memset(err, 0, sizeof(err));

  input_fd = mkstemp(input_template);
  TEST_CHECK(input_fd >= 0);
  wrote = write(input_fd, payload, sizeof(payload));
  TEST_CHECK(wrote == (ssize_t)sizeof(payload));
  TEST_CHECK(close(input_fd) == 0);

  output_fd = mkstemp(output_template);
  TEST_CHECK(output_fd >= 0);
  TEST_CHECK(close(output_fd) == 0);

  options.input_path = input_template;
  options.output_path = output_template;
  options.legend_enabled = 1;
  options.legend_path = input_template;
  options.auto_order = 1;

  TEST_CHECK(!hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(strstr(err, "refusing destructive path alias") != 0);
  test_assert_file_content(input_template, payload, sizeof(payload));

  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(output_template) == 0);
}

static void test_alias_output_equals_input_rejected(void)
{
  char input_template[] = "/tmp/hv_alias_same_XXXXXX";
  uint8_t payload[] = {0xAAu, 0xBBu, 0xCCu, 0xDDu};
  int input_fd = -1;
  ssize_t wrote = 0;
  HvRenderOptions options;
  HvRenderResult result;
  char err[256];

  memset(&options, 0, sizeof(options));
  memset(&result, 0, sizeof(result));
  memset(err, 0, sizeof(err));

  input_fd = mkstemp(input_template);
  TEST_CHECK(input_fd >= 0);
  wrote = write(input_fd, payload, sizeof(payload));
  TEST_CHECK(wrote == (ssize_t)sizeof(payload));
  TEST_CHECK(close(input_fd) == 0);

  options.input_path = input_template;
  options.output_path = input_template;
  options.auto_order = 1;

  TEST_CHECK(!hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(strstr(err, "refusing destructive path alias") != 0);
  test_assert_file_content(input_template, payload, sizeof(payload));

  TEST_CHECK(unlink(input_template) == 0);
}

static void test_alias_legend_equals_output_rejected(void)
{
  char input_template[] = "/tmp/hv_alias_lio_in_XXXXXX";
  char output_template[] = "/tmp/hv_alias_lio_out_XXXXXX";
  uint8_t payload[] = {0x01u, 0x02u, 0x03u, 0x04u};
  int input_fd = -1;
  int output_fd = -1;
  ssize_t wrote = 0;
  HvRenderOptions options;
  HvRenderResult result;
  char err[256];

  memset(&options, 0, sizeof(options));
  memset(&result, 0, sizeof(result));
  memset(err, 0, sizeof(err));

  input_fd = mkstemp(input_template);
  TEST_CHECK(input_fd >= 0);
  wrote = write(input_fd, payload, sizeof(payload));
  TEST_CHECK(wrote == (ssize_t)sizeof(payload));
  TEST_CHECK(close(input_fd) == 0);

  output_fd = mkstemp(output_template);
  TEST_CHECK(output_fd >= 0);
  TEST_CHECK(close(output_fd) == 0);

  options.input_path = input_template;
  options.output_path = output_template;
  options.legend_enabled = 1;
  options.legend_path = output_template;
  options.auto_order = 1;

  TEST_CHECK(!hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(strstr(err, "refusing destructive path alias") != 0);
  test_assert_file_content(input_template, payload, sizeof(payload));

  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(output_template) == 0);
}

static void test_alias_generated_page_equals_input_rejected(void)
{
  char base_template[] = "/tmp/hv_alias_pagebase_XXXXXX";
  char input_path[192];
  char output_base[192];
  uint8_t payload[10];
  int seed_fd = -1;
  int input_fd = -1;
  ssize_t wrote = 0;
  int i = 0;
  HvRenderOptions options;
  HvRenderResult result;
  char err[256];

  memset(&options, 0, sizeof(options));
  memset(&result, 0, sizeof(result));
  memset(err, 0, sizeof(err));

  for (i = 0; i < 10; ++i) {
    payload[i] = (uint8_t)i;
  }

  seed_fd = mkstemp(base_template);
  TEST_CHECK(seed_fd >= 0);
  TEST_CHECK(close(seed_fd) == 0);
  TEST_CHECK(unlink(base_template) == 0);

  TEST_CHECK(snprintf(input_path, sizeof(input_path), "%s_page0001.ppm", base_template) > 0);
  TEST_CHECK(snprintf(output_base, sizeof(output_base), "%s.ppm", base_template) > 0);

  input_fd = open(input_path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
  TEST_CHECK(input_fd >= 0);
  wrote = write(input_fd, payload, sizeof(payload));
  TEST_CHECK(wrote == (ssize_t)sizeof(payload));
  TEST_CHECK(close(input_fd) == 0);

  options.input_path = input_path;
  options.output_path = output_base;
  options.auto_order = 0;
  options.order = 1u;
  options.paginate = 1;

  TEST_CHECK(!hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(strstr(err, "refusing destructive path alias") != 0);
  test_assert_file_content(input_path, payload, sizeof(payload));

  TEST_CHECK(unlink(input_path) == 0);
}

static void test_alias_symlink_output_to_input_rejected(void)
{
  char input_template[] = "/tmp/hv_alias_symlink_in_XXXXXX";
  char link_template[] = "/tmp/hv_alias_symlink_out_XXXXXX";
  uint8_t payload[] = {0x10u, 0x20u, 0x30u, 0x40u};
  int input_fd = -1;
  int seed_fd = -1;
  ssize_t wrote = 0;
  HvRenderOptions options;
  HvRenderResult result;
  char err[256];

  memset(&options, 0, sizeof(options));
  memset(&result, 0, sizeof(result));
  memset(err, 0, sizeof(err));

  input_fd = mkstemp(input_template);
  TEST_CHECK(input_fd >= 0);
  wrote = write(input_fd, payload, sizeof(payload));
  TEST_CHECK(wrote == (ssize_t)sizeof(payload));
  TEST_CHECK(close(input_fd) == 0);

  seed_fd = mkstemp(link_template);
  TEST_CHECK(seed_fd >= 0);
  TEST_CHECK(close(seed_fd) == 0);
  TEST_CHECK(unlink(link_template) == 0);
  TEST_CHECK(symlink(input_template, link_template) == 0);

  options.input_path = input_template;
  options.output_path = link_template;
  options.auto_order = 1;

  TEST_CHECK(!hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(strstr(err, "refusing destructive path alias") != 0);
  test_assert_file_content(input_template, payload, sizeof(payload));

  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(link_template) == 0);
}

static void test_alias_legend_equals_generated_page_rejected(void)
{
  char base_template[] = "/tmp/hv_alias_legpage_XXXXXX";
  char input_path[192];
  char output_base[192];
  char legend_path[192];
  uint8_t payload[10];
  int seed_fd = -1;
  int input_fd = -1;
  ssize_t wrote = 0;
  int i = 0;
  HvRenderOptions options;
  HvRenderResult result;
  char err[256];

  memset(&options, 0, sizeof(options));
  memset(&result, 0, sizeof(result));
  memset(err, 0, sizeof(err));

  for (i = 0; i < 10; ++i) {
    payload[i] = (uint8_t)(i + 1);
  }

  seed_fd = mkstemp(base_template);
  TEST_CHECK(seed_fd >= 0);
  TEST_CHECK(close(seed_fd) == 0);
  TEST_CHECK(unlink(base_template) == 0);

  TEST_CHECK(snprintf(input_path, sizeof(input_path), "%s.input.bin", base_template) > 0);
  TEST_CHECK(snprintf(output_base, sizeof(output_base), "%s.ppm", base_template) > 0);
  TEST_CHECK(snprintf(legend_path, sizeof(legend_path), "%s_page0001.ppm", base_template) > 0);

  input_fd = open(input_path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
  TEST_CHECK(input_fd >= 0);
  wrote = write(input_fd, payload, sizeof(payload));
  TEST_CHECK(wrote == (ssize_t)sizeof(payload));
  TEST_CHECK(close(input_fd) == 0);

  options.input_path = input_path;
  options.output_path = output_base;
  options.auto_order = 0;
  options.order = 1u;
  options.paginate = 1;
  options.legend_enabled = 1;
  options.legend_path = legend_path;

  TEST_CHECK(!hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(strstr(err, "refusing destructive path alias") != 0);
  TEST_CHECK(strstr(err, "legend path") != 0);
  test_assert_file_content(input_path, payload, sizeof(payload));

  TEST_CHECK(unlink(input_path) == 0);
}

static void test_alias_hardlink_output_to_input_rejected(void)
{
  char input_template[] = "/tmp/hv_alias_hardlink_in_XXXXXX";
  char link_template[] = "/tmp/hv_alias_hardlink_out_XXXXXX";
  uint8_t payload[] = {0x61u, 0x62u, 0x63u, 0x64u};
  int input_fd = -1;
  int seed_fd = -1;
  ssize_t wrote = 0;
  HvRenderOptions options;
  HvRenderResult result;
  char err[256];

  memset(&options, 0, sizeof(options));
  memset(&result, 0, sizeof(result));
  memset(err, 0, sizeof(err));

  input_fd = mkstemp(input_template);
  TEST_CHECK(input_fd >= 0);
  wrote = write(input_fd, payload, sizeof(payload));
  TEST_CHECK(wrote == (ssize_t)sizeof(payload));
  TEST_CHECK(close(input_fd) == 0);

  seed_fd = mkstemp(link_template);
  TEST_CHECK(seed_fd >= 0);
  TEST_CHECK(close(seed_fd) == 0);
  TEST_CHECK(unlink(link_template) == 0);
  TEST_CHECK(link(input_template, link_template) == 0);

  options.input_path = input_template;
  options.output_path = link_template;
  options.auto_order = 1;

  TEST_CHECK(!hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(strstr(err, "refusing destructive path alias") != 0);
  test_assert_file_content(input_template, payload, sizeof(payload));

  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(link_template) == 0);
}

static void test_alias_race_generated_page_swapped_to_input_rejected(void)
{
  char base_template[] = "/tmp/hv_alias_race_XXXXXX";
  char input_path[192];
  char output_base[192];
  char page1_path[192];
  char page2_path[192];
  uint8_t *payload = 0;
  size_t payload_size = (size_t)(2u * (1u << 18u)); /* order 9 => 262144 bytes/page */
  int seed_fd = -1;
  int input_fd = -1;
  pid_t helper_pid = (pid_t)-1;
  int helper_status = 0;
  size_t i = 0u;
  HvRenderOptions options;
  HvRenderResult result;
  char err[256];

  memset(&options, 0, sizeof(options));
  memset(&result, 0, sizeof(result));
  memset(err, 0, sizeof(err));

  payload = (uint8_t *)malloc(payload_size);
  TEST_CHECK(payload != 0);
  for (i = 0u; i < payload_size; ++i) {
    payload[i] = (uint8_t)(i & 0xFFu);
  }

  seed_fd = mkstemp(base_template);
  TEST_CHECK(seed_fd >= 0);
  TEST_CHECK(close(seed_fd) == 0);
  TEST_CHECK(unlink(base_template) == 0);

  TEST_CHECK(snprintf(input_path, sizeof(input_path), "%s.input.bin", base_template) > 0);
  TEST_CHECK(snprintf(output_base, sizeof(output_base), "%s.ppm", base_template) > 0);
  TEST_CHECK(snprintf(page1_path, sizeof(page1_path), "%s_page0001.ppm", base_template) > 0);
  TEST_CHECK(snprintf(page2_path, sizeof(page2_path), "%s_page0002.ppm", base_template) > 0);

  input_fd = open(input_path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
  TEST_CHECK(input_fd >= 0);
  TEST_CHECK(test_write_all(input_fd, payload, payload_size));
  TEST_CHECK(close(input_fd) == 0);

  helper_pid = fork();
  TEST_CHECK(helper_pid >= 0);
  if (helper_pid == 0) {
    int seen_page1 = 0;
    int tries = 0;
    const struct timespec delay = {0, 1000000L};
    for (tries = 0; tries < 20000; ++tries) {
      if (access(page1_path, F_OK) == 0) {
        seen_page1 = 1;
        break;
      }
      (void)nanosleep(&delay, 0);
    }
    if (!seen_page1) {
      _exit(2);
    }
    (void)unlink(page2_path);
    if (symlink(input_path, page2_path) != 0) {
      _exit(3);
    }
    _exit(0);
  }

  options.input_path = input_path;
  options.output_path = output_base;
  options.auto_order = 0;
  options.order = 9u;
  options.paginate = 1;

  TEST_CHECK(!hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(strstr(err, "refusing destructive path alias") != 0);
  TEST_CHECK(strstr(err, "aliases input") != 0);

  TEST_CHECK(waitpid(helper_pid, &helper_status, 0) == helper_pid);
  TEST_CHECK(WIFEXITED(helper_status));
  TEST_CHECK(WEXITSTATUS(helper_status) == 0);

  test_assert_file_content(input_path, payload, payload_size);

  free(payload);
  payload = 0;
  TEST_CHECK(unlink(input_path) == 0);
  (void)unlink(page1_path);
  (void)unlink(page2_path);
}

#ifdef HV_TEST_HAVE_PNG
static void test_render_png_output(void)
{
  char input_template[] = "/tmp/hv_png_input_XXXXXX";
  char output_template[] = "/tmp/hv_png_output_XXXXXX";
  char output_path[128];
  uint8_t payload[] = {0x00u, 0x01u, 0x20u, 0x7Fu};
  int input_fd = -1;
  int output_fd = -1;
  HvRenderOptions options;
  HvRenderResult result;
  char err[256];
  FILE *out_fp = 0;
  uint8_t sig[8];
  ssize_t wrote = 0;

  memset(&options, 0, sizeof(options));
  memset(&result, 0, sizeof(result));
  memset(err, 0, sizeof(err));

  input_fd = mkstemp(input_template);
  TEST_CHECK(input_fd >= 0);
  wrote = write(input_fd, payload, sizeof(payload));
  TEST_CHECK(wrote == (ssize_t)sizeof(payload));
  TEST_CHECK(close(input_fd) == 0);

  output_fd = mkstemp(output_template);
  TEST_CHECK(output_fd >= 0);
  TEST_CHECK(close(output_fd) == 0);
  TEST_CHECK(unlink(output_template) == 0);

  TEST_CHECK(snprintf(output_path, sizeof(output_path), "%s.png", output_template) > 0);

  options.input_path = input_template;
  options.output_path = output_path;
  options.auto_order = 1;

  TEST_CHECK(hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(result.page_count == 1u);

  out_fp = fopen(output_path, "rb");
  TEST_CHECK(out_fp != 0);
  TEST_CHECK(fread(sig, 1u, sizeof(sig), out_fp) == sizeof(sig));
  TEST_CHECK(memcmp(sig, "\x89PNG\r\n\x1a\n", sizeof(sig)) == 0);
  TEST_CHECK(fclose(out_fp) == 0);

  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(output_path) == 0);
}

static void test_png_stream_rejects_size_overflow(void)
{
  FILE *fp = 0;
  uint8_t pixel[3] = {0u, 0u, 0u};
  char err[256];

  memset(err, 0, sizeof(err));
  fp = tmpfile();
  TEST_CHECK(fp != 0);
  TEST_CHECK(
    !hv_write_image_stream("overflow.png", fp, pixel, UINT32_MAX, UINT32_MAX, err, sizeof(err))
  );
  TEST_CHECK(strstr(err, "png image too large") != 0);
  TEST_CHECK(fclose(fp) == 0);
}
#endif

int main(void)
{
  test_hilbert_order_helpers();
  test_hilbert_pick_order();
  test_hilbert_d2xy_order1();
  test_hilbert_bijection();
  test_palette_edges();
  test_render_integration();
  test_render_paginate_and_legend();
  test_render_respects_max_image_cap();
  test_file_io_slice_and_stream_semantics();
  test_file_io_stream_detects_truncate_race();
  test_alias_legend_equals_input_rejected();
  test_alias_output_equals_input_rejected();
  test_alias_legend_equals_output_rejected();
  test_alias_generated_page_equals_input_rejected();
  test_alias_symlink_output_to_input_rejected();
  test_alias_legend_equals_generated_page_rejected();
  test_alias_hardlink_output_to_input_rejected();
  test_alias_race_generated_page_swapped_to_input_rejected();
#ifdef HV_TEST_HAVE_PNG
  test_render_png_output();
  test_png_stream_rejects_size_overflow();
#endif
  return 0;
}
