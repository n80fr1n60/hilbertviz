#include "3d_camera.h"
#include "3d_platform.h"
#include "3d_renderer.h"
#include "file_io.h"
#include "hilbert.h"
#include "hilbert3d.h"
#include "image.h"
#include "palette.h"
#include "point_cloud3d.h"
#include "png_writer.h"
#include "ppm.h"
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

static char *test_strdup_local(const char *text)
{
  size_t len = 0u;
  char *copy = 0;

  if (text == 0) {
    return 0;
  }

  len = strlen(text);
  copy = (char *)malloc(len + 1u);
  TEST_CHECK(copy != 0);
  memcpy(copy, text, len + 1u);
  return copy;
}

static char *test_save_env(const char *name)
{
  const char *value = getenv(name);
  if (value == 0) {
    return 0;
  }
  return test_strdup_local(value);
}

static void test_restore_env(const char *name, char *saved_value)
{
  if (saved_value != 0) {
    TEST_CHECK(setenv(name, saved_value, 1) == 0);
    free(saved_value);
  } else {
    TEST_CHECK(unsetenv(name) == 0);
  }
}

static double test_abs_double(double value)
{
  return (value < 0.0) ? -value : value;
}

static double test_normalized_axis(uint32_t coord, uint32_t side)
{
  return ((((double)coord + 0.5) / (double)side) * 2.0) - 1.0;
}

static void test_3d_camera_defaults(void)
{
  Hv3DCamera camera;

  memset(&camera, 0, sizeof(camera));
  hv_3d_camera_init_defaults(&camera);

  TEST_CHECK(test_abs_double((double)camera.yaw_degrees - 35.0) < 1e-6);
  TEST_CHECK(test_abs_double((double)camera.pitch_degrees - 25.0) < 1e-6);
  TEST_CHECK(test_abs_double((double)camera.distance - 3.0) < 1e-6);
  TEST_CHECK(test_abs_double((double)camera.target_x) < 1e-6);
  TEST_CHECK(test_abs_double((double)camera.target_y) < 1e-6);
  TEST_CHECK(test_abs_double((double)camera.target_z) < 1e-6);
  TEST_CHECK(camera.viewport_width == 1280u);
  TEST_CHECK(camera.viewport_height == 720u);
}

static void test_3d_camera_fit_cloud_small(void)
{
  Hv3DCamera camera;
  HvPointCloud3D cloud;

  memset(&camera, 0, sizeof(camera));
  memset(&cloud, 0, sizeof(cloud));

  hv_3d_camera_init_defaults(&camera);
  cloud.count = 4u;
  cloud.bounds.center_x = -0.5f;
  cloud.bounds.center_y = 0.0f;
  cloud.bounds.center_z = 0.0f;
  cloud.bounds.radius = 0.70710677f;

  TEST_CHECK(hv_3d_camera_fit_cloud(&camera, &cloud));
  TEST_CHECK(test_abs_double((double)camera.target_x - -0.5) < 1e-6);
  TEST_CHECK(test_abs_double((double)camera.target_y) < 1e-6);
  TEST_CHECK(test_abs_double((double)camera.target_z) < 1e-6);
  TEST_CHECK(test_abs_double((double)camera.distance - 1.5) < 1e-6);
}

static void test_3d_camera_fit_cloud_large(void)
{
  Hv3DCamera camera;
  HvPointCloud3D cloud;

  memset(&camera, 0, sizeof(camera));
  memset(&cloud, 0, sizeof(cloud));

  hv_3d_camera_init_defaults(&camera);
  cloud.count = 512u;
  cloud.bounds.radius = 1.51554441f;

  TEST_CHECK(hv_3d_camera_fit_cloud(&camera, &cloud));
  TEST_CHECK(test_abs_double((double)camera.target_x) < 1e-6);
  TEST_CHECK(test_abs_double((double)camera.target_y) < 1e-6);
  TEST_CHECK(test_abs_double((double)camera.target_z) < 1e-6);
  TEST_CHECK(test_abs_double((double)camera.distance - 2.52590728) < 1e-5);
}

static void test_3d_renderer_summary_output(void)
{
  HvPoint3D points[2];
  HvPointCloud3D cloud;
  Hv3DCamera camera;
  FILE *fp = 0;
  char err[256];
  char buf[512];
  size_t n = 0u;

  memset(points, 0, sizeof(points));
  memset(&cloud, 0, sizeof(cloud));
  memset(&camera, 0, sizeof(camera));
  memset(err, 0, sizeof(err));
  memset(buf, 0, sizeof(buf));

  hv_3d_camera_init_defaults(&camera);

  points[0].x = -0.5f;
  points[0].y = -0.5f;
  points[0].z = -0.5f;
  points[0].r = 0u;
  points[0].g = 0u;
  points[0].b = 0u;

  points[1].x = -0.5f;
  points[1].y = -0.5f;
  points[1].z = 0.5f;
  points[1].r = 255u;
  points[1].g = 0u;
  points[1].b = 0u;

  cloud.points = points;
  cloud.count = 2u;
  cloud.order = 1u;
  cloud.side = 2u;
  cloud.capacity = 8u;

  TEST_CHECK(!hv_3d_renderer_write_point_cloud_summary(0, &cloud, &camera, err, sizeof(err)));
  TEST_CHECK(strstr(err, "invalid arguments") != 0);

  memset(err, 0, sizeof(err));
  fp = tmpfile();
  TEST_CHECK(fp != 0);
  TEST_CHECK(hv_3d_renderer_write_point_cloud_summary(fp, &cloud, &camera, err, sizeof(err)));
  TEST_CHECK(fflush(fp) == 0);
  TEST_CHECK(fseek(fp, 0L, SEEK_SET) == 0);
  n = fread(buf, 1u, sizeof(buf) - 1u, fp);
  TEST_CHECK(ferror(fp) == 0);
  buf[n] = '\0';
  TEST_CHECK(strstr(buf, "Loaded point cloud: points=2 order=1 side=2 capacity=8") != 0);
  TEST_CHECK(strstr(buf, "First point: -0.500000 -0.500000 -0.500000 rgb=0,0,0") != 0);
  TEST_CHECK(strstr(buf, "Last point: -0.500000 -0.500000 0.500000 rgb=255,0,0") != 0);
  TEST_CHECK(fclose(fp) == 0);
}

static void test_3d_renderer_invalid_arguments(void)
{
  Hv3DRenderer renderer;
  Hv3DCamera camera;
  HvPointCloud3D cloud;
  char err[256];

  memset(&renderer, 0, sizeof(renderer));
  memset(&camera, 0, sizeof(camera));
  memset(&cloud, 0, sizeof(cloud));
  memset(err, 0, sizeof(err));

  TEST_CHECK(!hv_3d_renderer_validate_point_size(0.0f, err, sizeof(err)));
  TEST_CHECK(strstr(err, "invalid 3D point size") != 0);

  memset(err, 0, sizeof(err));
  TEST_CHECK(!hv_3d_renderer_init(0, 0, err, sizeof(err)));
  TEST_CHECK(strstr(err, "invalid arguments") != 0);

  memset(err, 0, sizeof(err));
  TEST_CHECK(!hv_3d_renderer_draw(0, &camera, 640u, 480u, HV_3D_POINT_SIZE_DEFAULT, err, sizeof(err)));
  TEST_CHECK(strstr(err, "invalid arguments") != 0);

  memset(err, 0, sizeof(err));
  if (!hv_3d_renderer_init(&renderer, &cloud, err, sizeof(err))) {
    TEST_CHECK(
      (strstr(err, "required OpenGL buffer functions are unavailable") != 0) ||
      (strstr(err, "3D renderer is unavailable in this build") != 0)
    );
  }
  hv_3d_renderer_shutdown(&renderer);
}

static void test_3d_platform_invalid_arguments(void)
{
  HvPointCloud3D cloud;
  Hv3DCamera camera;
  char *saved_hidden = 0;
  char *saved_frames = 0;
  char err[256];

  memset(&cloud, 0, sizeof(cloud));
  memset(&camera, 0, sizeof(camera));
  memset(err, 0, sizeof(err));
  TEST_CHECK(!hv_3d_platform_render_static_cloud(0, 0, HV_3D_POINT_SIZE_DEFAULT, err, sizeof(err)));
  TEST_CHECK(strstr(err, "invalid arguments") != 0);

  (void)hv_3d_platform_viewer_requested();

  saved_hidden = test_save_env("HILBERTVIZ3D_WINDOW_HIDDEN");
  saved_frames = test_save_env("HILBERTVIZ3D_RENDER_FRAMES");
  TEST_CHECK(setenv("HILBERTVIZ3D_WINDOW_HIDDEN", "1", 1) == 0);
  TEST_CHECK(setenv("HILBERTVIZ3D_RENDER_FRAMES", "bad", 1) == 0);

  memset(err, 0, sizeof(err));
  TEST_CHECK(!hv_3d_platform_render_static_cloud(&cloud, &camera, HV_3D_POINT_SIZE_DEFAULT, err, sizeof(err)));
  TEST_CHECK(strstr(err, "invalid HILBERTVIZ3D_RENDER_FRAMES='bad'") != 0);

  test_restore_env("HILBERTVIZ3D_WINDOW_HIDDEN", saved_hidden);
  test_restore_env("HILBERTVIZ3D_RENDER_FRAMES", saved_frames);
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

static void test_gilbert_rect_bijection(void)
{
  const uint32_t width = 5u;
  const uint32_t height = 4u;
  const uint64_t capacity = (uint64_t)width * (uint64_t)height;
  uint8_t *visited = 0;
  uint64_t d = 0u;

  visited = (uint8_t *)calloc((size_t)capacity, 1u);
  TEST_CHECK(visited != 0);

  for (d = 0u; d < capacity; ++d) {
    uint32_t x = 0u;
    uint32_t y = 0u;
    uint64_t idx = 0u;

    TEST_CHECK(hv_gilbert_d2xy(width, height, d, &x, &y));
    TEST_CHECK(x < width);
    TEST_CHECK(y < height);

    idx = ((uint64_t)y * (uint64_t)width) + (uint64_t)x;
    TEST_CHECK(idx < capacity);
    TEST_CHECK(visited[(size_t)idx] == 0u);
    visited[(size_t)idx] = 1u;
  }

  for (d = 0u; d < capacity; ++d) {
    TEST_CHECK(visited[(size_t)d] == 1u);
  }

  {
    uint32_t x = 0u;
    uint32_t y = 0u;
    TEST_CHECK(!hv_gilbert_d2xy(width, height, capacity, &x, &y));
  }
  free(visited);
}

static void test_gilbert_depth_limit_and_skinny_extremes(void)
{
  uint32_t x = 0u;
  uint32_t y = 0u;
  const uint32_t tall_height = UINT32_MAX;
  const uint64_t last_index = (uint64_t)tall_height - 1u;

  TEST_CHECK(!hv_gilbert_d2xy_with_limit(5u, 4u, 0u, 0u, &x, &y));
  TEST_CHECK(hv_gilbert_d2xy_with_limit(5u, 4u, 0u, 32u, &x, &y));

  TEST_CHECK(hv_gilbert_d2xy_with_limit(1u, tall_height, last_index, 0u, &x, &y));
  TEST_CHECK(x == 0u);
  TEST_CHECK(y == tall_height - 1u);
}

static void test_hilbert3d_order_helpers(void)
{
  uint32_t side = 0u;
  uint64_t capacity = 0u;

  TEST_CHECK(!hv_hilbert3d_side_for_order(0u, &side));
  TEST_CHECK(!hv_hilbert3d_side_for_order(8u, &side));
  TEST_CHECK(!hv_hilbert3d_side_for_order(1u, 0));
  TEST_CHECK(hv_hilbert3d_side_for_order(1u, &side));
  TEST_CHECK(side == 2u);
  TEST_CHECK(hv_hilbert3d_side_for_order(7u, &side));
  TEST_CHECK(side == 128u);

  TEST_CHECK(!hv_hilbert3d_capacity_for_order(0u, &capacity));
  TEST_CHECK(!hv_hilbert3d_capacity_for_order(8u, &capacity));
  TEST_CHECK(!hv_hilbert3d_capacity_for_order(1u, 0));
  TEST_CHECK(hv_hilbert3d_capacity_for_order(1u, &capacity));
  TEST_CHECK(capacity == 8u);
  TEST_CHECK(hv_hilbert3d_capacity_for_order(7u, &capacity));
  TEST_CHECK(capacity == 2097152u);
}

static void test_hilbert3d_d2xyz_order1(void)
{
  static const uint32_t expected[8][3] = {
    {0u, 0u, 0u},
    {0u, 0u, 1u},
    {0u, 1u, 1u},
    {0u, 1u, 0u},
    {1u, 1u, 0u},
    {1u, 1u, 1u},
    {1u, 0u, 1u},
    {1u, 0u, 0u}
  };
  uint64_t d = 0u;

  for (d = 0u; d < 8u; ++d) {
    uint32_t x = 0u;
    uint32_t y = 0u;
    uint32_t z = 0u;

    TEST_CHECK(hv_hilbert3d_d2xyz(1u, d, &x, &y, &z));
    TEST_CHECK(x == expected[d][0]);
    TEST_CHECK(y == expected[d][1]);
    TEST_CHECK(z == expected[d][2]);
  }
}

static void test_hilbert3d_bijection_and_adjacency(void)
{
  const uint32_t order = 2u;
  uint32_t side = 0u;
  uint64_t capacity = 0u;
  uint8_t *visited = 0;
  uint64_t d = 0u;
  uint32_t prev_x = 0u;
  uint32_t prev_y = 0u;
  uint32_t prev_z = 0u;

  TEST_CHECK(hv_hilbert3d_side_for_order(order, &side));
  TEST_CHECK(hv_hilbert3d_capacity_for_order(order, &capacity));

  visited = (uint8_t *)calloc((size_t)capacity, 1u);
  TEST_CHECK(visited != 0);

  for (d = 0u; d < capacity; ++d) {
    uint32_t x = 0u;
    uint32_t y = 0u;
    uint32_t z = 0u;
    uint64_t idx = 0u;

    TEST_CHECK(hv_hilbert3d_d2xyz(order, d, &x, &y, &z));
    TEST_CHECK(x < side);
    TEST_CHECK(y < side);
    TEST_CHECK(z < side);

    idx = (((uint64_t)z * (uint64_t)side) + (uint64_t)y) * (uint64_t)side + (uint64_t)x;
    TEST_CHECK(idx < capacity);
    TEST_CHECK(visited[(size_t)idx] == 0u);
    visited[(size_t)idx] = 1u;

    if (d != 0u) {
      uint32_t dx = (x > prev_x) ? (x - prev_x) : (prev_x - x);
      uint32_t dy = (y > prev_y) ? (y - prev_y) : (prev_y - y);
      uint32_t dz = (z > prev_z) ? (z - prev_z) : (prev_z - z);

      TEST_CHECK((dx + dy + dz) == 1u);
    }

    prev_x = x;
    prev_y = y;
    prev_z = z;
  }

  for (d = 0u; d < capacity; ++d) {
    TEST_CHECK(visited[(size_t)d] == 1u);
  }

  TEST_CHECK(!hv_hilbert3d_d2xyz(order, capacity, &prev_x, &prev_y, &prev_z));
  TEST_CHECK(!hv_hilbert3d_d2xyz(0u, 0u, &prev_x, &prev_y, &prev_z));
  TEST_CHECK(!hv_hilbert3d_d2xyz(8u, 0u, &prev_x, &prev_y, &prev_z));
  TEST_CHECK(!hv_hilbert3d_d2xyz(order, 0u, 0, &prev_y, &prev_z));

  free(visited);
}

static void test_point_cloud3d_empty_slice(void)
{
  char input_template[] = "/tmp/hv_point_cloud3d_empty_XXXXXX";
  int input_fd = -1;
  HvPointCloud3D cloud;
  char err[256];

  memset(&cloud, 0, sizeof(cloud));
  memset(err, 0, sizeof(err));

  input_fd = mkstemp(input_template);
  TEST_CHECK(input_fd >= 0);
  TEST_CHECK(close(input_fd) == 0);

  TEST_CHECK(hv_build_point_cloud3d(input_template, 1u, 0u, 1, 0u, &cloud, err, sizeof(err)));
  TEST_CHECK(cloud.points == 0);
  TEST_CHECK(cloud.count == 0u);
  TEST_CHECK(cloud.order == 1u);
  TEST_CHECK(cloud.side == 2u);
  TEST_CHECK(cloud.capacity == 8u);
  TEST_CHECK(test_abs_double((double)cloud.bounds.radius) < 1e-6);
  TEST_CHECK(test_abs_double((double)cloud.bounds.center_x) < 1e-6);
  TEST_CHECK(test_abs_double((double)cloud.bounds.center_y) < 1e-6);
  TEST_CHECK(test_abs_double((double)cloud.bounds.center_z) < 1e-6);

  hv_free_point_cloud3d(&cloud);
  TEST_CHECK(unlink(input_template) == 0);
}

static void test_point_cloud3d_small_deterministic(void)
{
  char input_template[] = "/tmp/hv_point_cloud3d_small_XXXXXX";
  const uint8_t payload[] = {0x00u, 0x1Fu, 0x20u, 0xFFu};
  const uint8_t expected_rgb[][3] = {
    {0u, 0u, 0u},
    {0u, 255u, 0u},
    {0u, 0u, 32u},
    {255u, 0u, 0u}
  };
  const uint32_t expected_xyz[][3] = {
    {0u, 0u, 0u},
    {0u, 0u, 1u},
    {0u, 1u, 1u},
    {0u, 1u, 0u}
  };
  int input_fd = -1;
  ssize_t wrote = 0;
  HvPointCloud3D cloud;
  size_t i = 0u;
  char err[256];

  memset(&cloud, 0, sizeof(cloud));
  memset(err, 0, sizeof(err));

  input_fd = mkstemp(input_template);
  TEST_CHECK(input_fd >= 0);
  wrote = write(input_fd, payload, sizeof(payload));
  TEST_CHECK(wrote == (ssize_t)sizeof(payload));
  TEST_CHECK(close(input_fd) == 0);

  TEST_CHECK(hv_build_point_cloud3d(input_template, 1u, 0u, 0, 0u, &cloud, err, sizeof(err)));
  TEST_CHECK(cloud.points != 0);
  TEST_CHECK(cloud.count == sizeof(payload));
  TEST_CHECK(cloud.order == 1u);
  TEST_CHECK(cloud.side == 2u);
  TEST_CHECK(cloud.capacity == 8u);
  TEST_CHECK(test_abs_double((double)cloud.bounds.min_x - -0.5) < 1e-6);
  TEST_CHECK(test_abs_double((double)cloud.bounds.max_x - -0.5) < 1e-6);
  TEST_CHECK(test_abs_double((double)cloud.bounds.min_y - -0.5) < 1e-6);
  TEST_CHECK(test_abs_double((double)cloud.bounds.max_y - 0.5) < 1e-6);
  TEST_CHECK(test_abs_double((double)cloud.bounds.min_z - -0.5) < 1e-6);
  TEST_CHECK(test_abs_double((double)cloud.bounds.max_z - 0.5) < 1e-6);
  TEST_CHECK(test_abs_double((double)cloud.bounds.center_x - -0.5) < 1e-6);
  TEST_CHECK(test_abs_double((double)cloud.bounds.center_y) < 1e-6);
  TEST_CHECK(test_abs_double((double)cloud.bounds.center_z) < 1e-6);
  TEST_CHECK(test_abs_double((double)cloud.bounds.radius - 0.70710678) < 1e-6);

  for (i = 0u; i < cloud.count; ++i) {
    TEST_CHECK(test_abs_double((double)cloud.points[i].x - test_normalized_axis(expected_xyz[i][0], 2u)) < 1e-6);
    TEST_CHECK(test_abs_double((double)cloud.points[i].y - test_normalized_axis(expected_xyz[i][1], 2u)) < 1e-6);
    TEST_CHECK(test_abs_double((double)cloud.points[i].z - test_normalized_axis(expected_xyz[i][2], 2u)) < 1e-6);
    TEST_CHECK(cloud.points[i].r == expected_rgb[i][0]);
    TEST_CHECK(cloud.points[i].g == expected_rgb[i][1]);
    TEST_CHECK(cloud.points[i].b == expected_rgb[i][2]);
  }

  hv_free_point_cloud3d(&cloud);
  TEST_CHECK(unlink(input_template) == 0);
}

static void test_point_cloud3d_near_capacity_slice(void)
{
  char input_template[] = "/tmp/hv_point_cloud3d_near_capacity_XXXXXX";
  const uint32_t order = 3u;
  uint64_t capacity = 0u;
  uint64_t last_d = 0u;
  uint32_t last_x = 0u;
  uint32_t last_y = 0u;
  uint32_t last_z = 0u;
  HvPointCloud3D cloud;
  uint8_t *payload = 0;
  int input_fd = -1;
  ssize_t wrote = 0;
  size_t payload_size = 0u;
  char err[256];

  memset(&cloud, 0, sizeof(cloud));
  memset(err, 0, sizeof(err));

  TEST_CHECK(hv_hilbert3d_capacity_for_order(order, &capacity));
  TEST_CHECK(capacity > 1u);
  payload_size = (size_t)(capacity - 1u);
  payload = (uint8_t *)malloc(payload_size);
  TEST_CHECK(payload != 0);
  memset(payload, 0x7Fu, payload_size);

  input_fd = mkstemp(input_template);
  TEST_CHECK(input_fd >= 0);
  wrote = write(input_fd, payload, payload_size);
  TEST_CHECK(wrote == (ssize_t)payload_size);
  TEST_CHECK(close(input_fd) == 0);

  TEST_CHECK(hv_build_point_cloud3d(input_template, order, 0u, 0, 0u, &cloud, err, sizeof(err)));
  TEST_CHECK(cloud.count == payload_size);
  TEST_CHECK(cloud.side == 8u);
  TEST_CHECK(cloud.capacity == capacity);
  TEST_CHECK(test_abs_double((double)cloud.bounds.center_x) < 0.2);
  TEST_CHECK(test_abs_double((double)cloud.bounds.center_y) < 0.2);
  TEST_CHECK(test_abs_double((double)cloud.bounds.center_z) < 0.2);
  TEST_CHECK((double)cloud.bounds.radius > 1.4);

  last_d = capacity - 2u;
  TEST_CHECK(hv_hilbert3d_d2xyz(order, last_d, &last_x, &last_y, &last_z));
  TEST_CHECK(test_abs_double((double)cloud.points[cloud.count - 1u].x - test_normalized_axis(last_x, cloud.side)) < 1e-6);
  TEST_CHECK(test_abs_double((double)cloud.points[cloud.count - 1u].y - test_normalized_axis(last_y, cloud.side)) < 1e-6);
  TEST_CHECK(test_abs_double((double)cloud.points[cloud.count - 1u].z - test_normalized_axis(last_z, cloud.side)) < 1e-6);
  TEST_CHECK(cloud.points[cloud.count - 1u].r == 32u);
  TEST_CHECK(cloud.points[cloud.count - 1u].g == 0u);
  TEST_CHECK(cloud.points[cloud.count - 1u].b == 0u);

  hv_free_point_cloud3d(&cloud);
  free(payload);
  TEST_CHECK(unlink(input_template) == 0);
}

static void test_point_cloud3d_offset_and_length(void)
{
  char input_template[] = "/tmp/hv_point_cloud3d_slice_XXXXXX";
  const uint8_t payload[] = {0xAAu, 0xBBu, 0x00u, 0xFFu, 0x11u};
  int input_fd = -1;
  ssize_t wrote = 0;
  HvPointCloud3D cloud;
  char err[256];

  memset(&cloud, 0, sizeof(cloud));
  memset(err, 0, sizeof(err));

  input_fd = mkstemp(input_template);
  TEST_CHECK(input_fd >= 0);
  wrote = write(input_fd, payload, sizeof(payload));
  TEST_CHECK(wrote == (ssize_t)sizeof(payload));
  TEST_CHECK(close(input_fd) == 0);

  TEST_CHECK(hv_build_point_cloud3d(input_template, 1u, 2u, 1, 2u, &cloud, err, sizeof(err)));
  TEST_CHECK(cloud.count == 2u);
  TEST_CHECK(test_abs_double((double)cloud.points[0].x - test_normalized_axis(0u, 2u)) < 1e-6);
  TEST_CHECK(test_abs_double((double)cloud.points[0].y - test_normalized_axis(0u, 2u)) < 1e-6);
  TEST_CHECK(test_abs_double((double)cloud.points[0].z - test_normalized_axis(0u, 2u)) < 1e-6);
  TEST_CHECK(cloud.points[0].r == 0u);
  TEST_CHECK(cloud.points[0].g == 0u);
  TEST_CHECK(cloud.points[0].b == 0u);
  TEST_CHECK(test_abs_double((double)cloud.points[1].z - test_normalized_axis(1u, 2u)) < 1e-6);
  TEST_CHECK(cloud.points[1].r == 255u);
  TEST_CHECK(cloud.points[1].g == 0u);
  TEST_CHECK(cloud.points[1].b == 0u);
  TEST_CHECK(test_abs_double((double)cloud.bounds.center_x - -0.5) < 1e-6);
  TEST_CHECK(test_abs_double((double)cloud.bounds.center_y - -0.5) < 1e-6);
  TEST_CHECK(test_abs_double((double)cloud.bounds.center_z) < 1e-6);
  TEST_CHECK(test_abs_double((double)cloud.bounds.radius - 0.5) < 1e-6);

  hv_free_point_cloud3d(&cloud);
  TEST_CHECK(unlink(input_template) == 0);
}

static void test_point_cloud3d_invalid_arguments(void)
{
  HvPointCloud3D cloud;
  char err[256];

  memset(&cloud, 0, sizeof(cloud));
  memset(err, 0, sizeof(err));

  TEST_CHECK(!hv_build_point_cloud3d(0, 1u, 0u, 0, 0u, &cloud, err, sizeof(err)));
  TEST_CHECK(strstr(err, "invalid arguments for 3D point cloud build") != 0);
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

static void test_image_api_dispatch_and_errors(void)
{
  char base_template[] = "/tmp/hv_image_dispatch_XXXXXX";
  char path_ppm[192];
  char path_png[192];
  char path_noext[192];
  char path_bad[192];
  uint8_t pixels[12] = {
    0x00u, 0x00u, 0x00u,
    0x11u, 0x22u, 0x33u,
    0x44u, 0x55u, 0x66u,
    0x77u, 0x88u, 0x99u
  };
  int seed_fd = -1;
  char err[256];
  FILE *fp = 0;
  struct stat st;

  memset(err, 0, sizeof(err));

  seed_fd = mkstemp(base_template);
  TEST_CHECK(seed_fd >= 0);
  TEST_CHECK(close(seed_fd) == 0);
  TEST_CHECK(unlink(base_template) == 0);

  TEST_CHECK(snprintf(path_ppm, sizeof(path_ppm), "%s.ppm", base_template) > 0);
  TEST_CHECK(snprintf(path_png, sizeof(path_png), "%s.png", base_template) > 0);
  TEST_CHECK(snprintf(path_noext, sizeof(path_noext), "%s_noext", base_template) > 0);
  TEST_CHECK(snprintf(path_bad, sizeof(path_bad), "%s.bad", base_template) > 0);

  TEST_CHECK(!hv_write_image(0, pixels, 2u, 2u, err, sizeof(err)));
  TEST_CHECK(strstr(err, "missing output path") != 0);

  memset(err, 0, sizeof(err));
  TEST_CHECK(hv_write_image(path_ppm, pixels, 2u, 2u, err, sizeof(err)));
  TEST_CHECK(stat(path_ppm, &st) == 0);
  TEST_CHECK(st.st_size > 0);

  memset(err, 0, sizeof(err));
  TEST_CHECK(hv_write_image(path_noext, pixels, 2u, 2u, err, sizeof(err)));
  TEST_CHECK(stat(path_noext, &st) == 0);
  TEST_CHECK(st.st_size > 0);

#ifdef HV_TEST_HAVE_PNG
  memset(err, 0, sizeof(err));
  TEST_CHECK(hv_write_image(path_png, pixels, 2u, 2u, err, sizeof(err)));
  TEST_CHECK(stat(path_png, &st) == 0);
  TEST_CHECK(st.st_size > 0);
#else
  memset(err, 0, sizeof(err));
  TEST_CHECK(!hv_write_image(path_png, pixels, 2u, 2u, err, sizeof(err)));
  TEST_CHECK(strstr(err, "libpng is not available") != 0);
#endif

  memset(err, 0, sizeof(err));
  TEST_CHECK(!hv_write_image(path_bad, pixels, 2u, 2u, err, sizeof(err)));
  TEST_CHECK(strstr(err, "unsupported output extension") != 0);

  memset(err, 0, sizeof(err));
  TEST_CHECK(!hv_write_image("x.unsupported", pixels, 2u, 2u, err, sizeof(err)));
  TEST_CHECK(strstr(err, "unsupported output extension") != 0);

  fp = tmpfile();
  TEST_CHECK(fp != 0);

  memset(err, 0, sizeof(err));
  TEST_CHECK(!hv_write_image_stream(0, fp, pixels, 2u, 2u, err, sizeof(err)));
  TEST_CHECK(strstr(err, "missing output path") != 0);

  memset(err, 0, sizeof(err));
  TEST_CHECK(!hv_write_image_stream("dummy.ppm", 0, pixels, 2u, 2u, err, sizeof(err)));
  TEST_CHECK(strstr(err, "missing output stream") != 0);

  memset(err, 0, sizeof(err));
  TEST_CHECK(!hv_write_image_stream("dummy.bad", fp, pixels, 2u, 2u, err, sizeof(err)));
  TEST_CHECK(strstr(err, "unsupported output extension") != 0);

  TEST_CHECK(fclose(fp) == 0);

  TEST_CHECK(unlink(path_ppm) == 0);
  TEST_CHECK(unlink(path_noext) == 0);
  (void)unlink(path_png);
  (void)unlink(path_bad);
}

static void test_ppm_writer_wrapper_and_error_paths(void)
{
  char base_template[] = "/tmp/hv_ppm_wrap_XXXXXX";
  char missing_dir_template[] = "/tmp/hv_ppm_missing_XXXXXX";
  char path_ppm[192];
  char path_missing[192];
  uint8_t pixels[12] = {
    0x00u, 0x00u, 0x00u,
    0x11u, 0x22u, 0x33u,
    0x44u, 0x55u, 0x66u,
    0x77u, 0x88u, 0x99u
  };
  int seed_fd = -1;
  char *missing_dir = 0;
  char err[256];
  FILE *ro = 0;
  struct stat st;

  memset(err, 0, sizeof(err));

  seed_fd = mkstemp(base_template);
  TEST_CHECK(seed_fd >= 0);
  TEST_CHECK(close(seed_fd) == 0);
  TEST_CHECK(unlink(base_template) == 0);
  TEST_CHECK(snprintf(path_ppm, sizeof(path_ppm), "%s.ppm", base_template) > 0);

  missing_dir = mkdtemp(missing_dir_template);
  TEST_CHECK(missing_dir != 0);
  TEST_CHECK(rmdir(missing_dir) == 0);
  TEST_CHECK(snprintf(path_missing, sizeof(path_missing), "%s/out.ppm", missing_dir_template) > 0);

  TEST_CHECK(!hv_write_ppm(0, pixels, 2u, 2u, err, sizeof(err)));
  TEST_CHECK(strstr(err, "invalid arguments") != 0);

  memset(err, 0, sizeof(err));
  TEST_CHECK(!hv_write_ppm(path_missing, pixels, 2u, 2u, err, sizeof(err)));
  TEST_CHECK(strstr(err, "failed to open output") != 0);

  memset(err, 0, sizeof(err));
  TEST_CHECK(hv_write_ppm(path_ppm, pixels, 2u, 2u, err, sizeof(err)));
  TEST_CHECK(stat(path_ppm, &st) == 0);
  TEST_CHECK(st.st_size > 0);

  memset(err, 0, sizeof(err));
  TEST_CHECK(!hv_write_ppm(path_ppm, pixels, UINT32_MAX, UINT32_MAX, err, sizeof(err)));
  TEST_CHECK(strstr(err, "image too large for host size_t") != 0);

  memset(err, 0, sizeof(err));
  TEST_CHECK(!hv_write_ppm_stream(0, path_ppm, pixels, 2u, 2u, err, sizeof(err)));
  TEST_CHECK(strstr(err, "invalid arguments") != 0);

  ro = fopen(path_ppm, "rb");
  TEST_CHECK(ro != 0);
  memset(err, 0, sizeof(err));
  TEST_CHECK(!hv_write_ppm_stream(ro, path_ppm, pixels, 2u, 2u, err, sizeof(err)));
  TEST_CHECK(strstr(err, "failed to write ppm header") != 0);
  TEST_CHECK(fclose(ro) == 0);

  TEST_CHECK(unlink(path_ppm) == 0);
}

static void test_png_writer_wrapper_and_error_paths(void)
{
  char base_template[] = "/tmp/hv_png_wrap_XXXXXX";
  char missing_dir_template[] = "/tmp/hv_png_missing_XXXXXX";
  char path_png[192];
  char path_missing[192];
  uint8_t pixels[12] = {
    0x00u, 0x00u, 0x00u,
    0x11u, 0x22u, 0x33u,
    0x44u, 0x55u, 0x66u,
    0x77u, 0x88u, 0x99u
  };
  int seed_fd = -1;
  char *missing_dir = 0;
  char err[256];
  FILE *fp = 0;

  memset(err, 0, sizeof(err));

  seed_fd = mkstemp(base_template);
  TEST_CHECK(seed_fd >= 0);
  TEST_CHECK(close(seed_fd) == 0);
  TEST_CHECK(unlink(base_template) == 0);
  TEST_CHECK(snprintf(path_png, sizeof(path_png), "%s.png", base_template) > 0);

  missing_dir = mkdtemp(missing_dir_template);
  TEST_CHECK(missing_dir != 0);
  TEST_CHECK(rmdir(missing_dir) == 0);
  TEST_CHECK(snprintf(path_missing, sizeof(path_missing), "%s/out.png", missing_dir_template) > 0);

#ifdef HV_TEST_HAVE_PNG
  TEST_CHECK(!hv_write_png(0, pixels, 2u, 2u, err, sizeof(err)));
  TEST_CHECK(strstr(err, "invalid arguments") != 0);

  memset(err, 0, sizeof(err));
  TEST_CHECK(!hv_write_png(path_missing, pixels, 2u, 2u, err, sizeof(err)));
  TEST_CHECK(strstr(err, "failed to open output") != 0);

  memset(err, 0, sizeof(err));
  TEST_CHECK(hv_write_png(path_png, pixels, 2u, 2u, err, sizeof(err)));

  memset(err, 0, sizeof(err));
  TEST_CHECK(!hv_write_png(path_png, pixels, UINT32_MAX, 1u, err, sizeof(err)));
  TEST_CHECK(strstr(err, "libpng failed while writing png stream") != 0);

  memset(err, 0, sizeof(err));
  TEST_CHECK(!hv_write_png_stream(0, path_png, pixels, 2u, 2u, err, sizeof(err)));
  TEST_CHECK(strstr(err, "invalid arguments") != 0);

  fp = tmpfile();
  TEST_CHECK(fp != 0);
  memset(err, 0, sizeof(err));
  TEST_CHECK(!hv_write_png_stream(fp, path_png, pixels, UINT32_MAX, 1u, err, sizeof(err)));
  TEST_CHECK(strstr(err, "libpng failed while writing png stream") != 0);
  TEST_CHECK(fclose(fp) == 0);

  TEST_CHECK(unlink(path_png) == 0);
#else
  TEST_CHECK(!hv_write_png(path_png, pixels, 2u, 2u, err, sizeof(err)));
  TEST_CHECK(strstr(err, "libpng is not available") != 0);

  fp = tmpfile();
  TEST_CHECK(fp != 0);
  memset(err, 0, sizeof(err));
  TEST_CHECK(!hv_write_png_stream(fp, path_png, pixels, 2u, 2u, err, sizeof(err)));
  TEST_CHECK(strstr(err, "libpng is not available") != 0);
  TEST_CHECK(fclose(fp) == 0);
#endif
}

static void test_image_writers_null_error_buffer_paths(void)
{
  uint8_t pixel[3] = {0u, 0u, 0u};

  TEST_CHECK(!hv_write_ppm(0, pixel, 1u, 1u, 0, 0u));
  TEST_CHECK(!hv_write_ppm_stream(0, "x.ppm", pixel, 1u, 1u, 0, 0u));

#ifdef HV_TEST_HAVE_PNG
  TEST_CHECK(!hv_write_png(0, pixel, 1u, 1u, 0, 0u));
  TEST_CHECK(!hv_write_png_stream(0, "x.png", pixel, 1u, 1u, 0, 0u));
#else
  FILE *fp = 0;
  fp = tmpfile();
  TEST_CHECK(fp != 0);
  TEST_CHECK(!hv_write_png("x.png", pixel, 1u, 1u, 0, 0u));
  TEST_CHECK(!hv_write_png_stream(fp, "x.png", pixel, 1u, 1u, 0, 0u));
  TEST_CHECK(fclose(fp) == 0);
#endif
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

static void test_render_rect_hilbert_integration(void)
{
  char input_template[] = "/tmp/hv_rect_input_XXXXXX";
  char output_template[] = "/tmp/hv_rect_output_XXXXXX";
  uint8_t payload[] = {0x00u, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u};
  int input_fd = -1;
  int output_fd = -1;
  HvRenderOptions options;
  HvRenderResult result;
  char err[256];
  struct stat out_stat;
  FILE *out_file = 0;
  char header[16];
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
  options.layout = HV_LAYOUT_RECT_HILBERT;
  options.dimensions_set = 1;
  options.width = 3u;
  options.height = 2u;
  options.paginate = 0;

  TEST_CHECK(hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(result.input_bytes == sizeof(payload));
  TEST_CHECK(result.capacity == 6u);
  TEST_CHECK(result.page_count == 1u);
  TEST_CHECK(result.order == 0u);
  TEST_CHECK(result.side == 0u);

  TEST_CHECK(stat(output_template, &out_stat) == 0);
  TEST_CHECK((uint64_t)out_stat.st_size == 29u);

  out_file = fopen(output_template, "rb");
  TEST_CHECK(out_file != 0);
  TEST_CHECK(fread(header, 1u, 11u, out_file) == 11u);
  TEST_CHECK(memcmp(header, "P6\n3 2\n255\n", 11u) == 0);
  TEST_CHECK(fclose(out_file) == 0);

  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(output_template) == 0);
}

static void test_render_rect_hilbert_strict_rejects_parity(void)
{
  char input_template[] = "/tmp/hv_rect_strict_input_XXXXXX";
  char output_template[] = "/tmp/hv_rect_strict_output_XXXXXX";
  uint8_t payload[] = {0x10u, 0x11u, 0x12u, 0x13u};
  int input_fd = -1;
  int output_fd = -1;
  HvRenderOptions options;
  HvRenderResult result;
  char err[256];
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
  options.layout = HV_LAYOUT_RECT_HILBERT;
  options.dimensions_set = 1;
  options.width = 3u;  /* larger odd */
  options.height = 2u; /* smaller even */
  options.strict_adjacency = 1;

  TEST_CHECK(!hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(strstr(err, "strict adjacency rejects") != 0);

  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(output_template) == 0);
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

static void test_render_output_char_device_succeeds(void)
{
  char input_template[] = "/tmp/hv_devnull_input_XXXXXX";
  uint8_t payload[] = {0x00u, 0x01u, 0x20u, 0x7Fu};
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
  options.output_path = "/dev/null";
  options.auto_order = 1;

  TEST_CHECK(hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(result.page_count == 1u);
  TEST_CHECK(result.input_bytes == (uint64_t)sizeof(payload));

  TEST_CHECK(unlink(input_template) == 0);
}

static void test_entropy_single_symbol_zero(void)
{
  char input_template[] = "/tmp/hv_entropy_zero_input_XXXXXX";
  uint8_t payload[] = {0xAAu, 0xAAu, 0xAAu, 0xAAu};
  int input_fd = -1;
  ssize_t wrote = 0;
  double entropy = -1.0;
  char err[256];

  memset(err, 0, sizeof(err));

  input_fd = mkstemp(input_template);
  TEST_CHECK(input_fd >= 0);
  wrote = write(input_fd, payload, sizeof(payload));
  TEST_CHECK(wrote == (ssize_t)sizeof(payload));
  TEST_CHECK(close(input_fd) == 0);

  TEST_CHECK(hv_compute_slice_entropy(input_template, 0u, 0, 0u, &entropy, err, sizeof(err)));
  TEST_CHECK(test_abs_double(entropy - 0.0) < 1e-9);

  TEST_CHECK(unlink(input_template) == 0);
}

static void test_entropy_balanced_two_symbol_slice(void)
{
  char input_template[] = "/tmp/hv_entropy_slice_input_XXXXXX";
  uint8_t payload[] = {0x10u, 0x10u, 0x10u, 0x10u, 0x00u, 0xFFu, 0x00u, 0xFFu};
  int input_fd = -1;
  ssize_t wrote = 0;
  double entropy = -1.0;
  char err[256];

  memset(err, 0, sizeof(err));

  input_fd = mkstemp(input_template);
  TEST_CHECK(input_fd >= 0);
  wrote = write(input_fd, payload, sizeof(payload));
  TEST_CHECK(wrote == (ssize_t)sizeof(payload));
  TEST_CHECK(close(input_fd) == 0);

  TEST_CHECK(hv_compute_slice_entropy(input_template, 4u, 1, 4u, &entropy, err, sizeof(err)));
  TEST_CHECK(test_abs_double(entropy - 1.0) < 1e-9);

  TEST_CHECK(unlink(input_template) == 0);
}

static void test_render_entropy_and_legend_key(void)
{
  char input_template[] = "/tmp/hv_entropy_render_input_XXXXXX";
  char output_template[] = "/tmp/hv_entropy_render_output_XXXXXX";
  char legend_path[160];
  uint8_t payload[] = {0x00u, 0xFFu, 0x00u, 0xFFu};
  int input_fd = -1;
  int output_fd = -1;
  ssize_t wrote = 0;
  HvRenderOptions options;
  HvRenderResult result;
  char err[256];
  FILE *legend_fp = 0;
  char legend_buf[1024];
  size_t legend_n = 0u;

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

  TEST_CHECK(snprintf(legend_path, sizeof(legend_path), "%s.legend.txt", output_template) > 0);

  options.input_path = input_template;
  options.output_path = output_template;
  options.legend_enabled = 1;
  options.legend_path = legend_path;
  options.auto_order = 1;

  TEST_CHECK(hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(test_abs_double(result.entropy_bits_per_byte - 1.0) < 1e-9);

  legend_fp = fopen(legend_path, "rb");
  TEST_CHECK(legend_fp != 0);
  legend_n = fread(legend_buf, 1u, sizeof(legend_buf) - 1u, legend_fp);
  TEST_CHECK(legend_n > 0u);
  legend_buf[legend_n] = '\0';
  TEST_CHECK(strstr(legend_buf, "entropy_bits_per_byte=1.000000") != 0);
  TEST_CHECK(fclose(legend_fp) == 0);

  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(output_template) == 0);
  TEST_CHECK(unlink(legend_path) == 0);
}

static void test_render_missing_input_reports_open_error(void)
{
  char missing_template[] = "/tmp/hv_missing_render_input_XXXXXX";
  char output_template[] = "/tmp/hv_missing_render_output_XXXXXX";
  int missing_fd = -1;
  int output_fd = -1;
  HvRenderOptions options;
  HvRenderResult result;
  char err[256];

  memset(&options, 0, sizeof(options));
  memset(&result, 0, sizeof(result));
  memset(err, 0, sizeof(err));

  missing_fd = mkstemp(missing_template);
  TEST_CHECK(missing_fd >= 0);
  TEST_CHECK(close(missing_fd) == 0);
  TEST_CHECK(unlink(missing_template) == 0);

  output_fd = mkstemp(output_template);
  TEST_CHECK(output_fd >= 0);
  TEST_CHECK(close(output_fd) == 0);

  options.input_path = missing_template;
  options.output_path = output_template;
  options.auto_order = 1;

  TEST_CHECK(!hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(strstr(err, "failed to open input") != 0);

  TEST_CHECK(unlink(output_template) == 0);
}

static void test_render_auto_paginate_small_uses_auto_order(void)
{
  char input_template[] = "/tmp/hv_auto_paginate_small_input_XXXXXX";
  char output_template[] = "/tmp/hv_auto_paginate_small_output_XXXXXX";
  uint8_t payload[] = {0x01u, 0x02u, 0x03u, 0x04u, 0x05u};
  int input_fd = -1;
  int output_fd = -1;
  ssize_t wrote = 0;
  HvRenderOptions options;
  HvRenderResult result;
  char err[256];
  struct stat out_stat;

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
  options.paginate = 1;

  TEST_CHECK(hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(result.order == 2u);
  TEST_CHECK(result.side == 4u);
  TEST_CHECK(result.capacity == 16u);
  TEST_CHECK(result.input_bytes == (uint64_t)sizeof(payload));
  TEST_CHECK(result.page_count == 1u);
  TEST_CHECK(stat(output_template, &out_stat) == 0);
  TEST_CHECK(out_stat.st_size > 0);

  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(output_template) == 0);
}

static void test_render_auto_paginate_large_uses_default_page_order(void)
{
  char input_template[] = "/tmp/hv_auto_paginate_large_input_XXXXXX";
  char output_seed[] = "/tmp/hv_auto_paginate_large_output_XXXXXX";
  char output_path[192];
  char page1_path[192];
  char page2_path[192];
  const off_t large_size = (off_t)((1u << 24u) + 1u);
  const char *ext = ".ppm";
  int input_fd = -1;
  int output_fd = -1;
  HvRenderOptions options;
  HvRenderResult result;
  char err[256];
  struct stat out_stat;

  memset(&options, 0, sizeof(options));
  memset(&result, 0, sizeof(result));
  memset(err, 0, sizeof(err));

#ifdef HV_TEST_HAVE_PNG
  ext = ".png";
#endif

  input_fd = mkstemp(input_template);
  TEST_CHECK(input_fd >= 0);
  TEST_CHECK(ftruncate(input_fd, large_size) == 0);
  TEST_CHECK(close(input_fd) == 0);

  output_fd = mkstemp(output_seed);
  TEST_CHECK(output_fd >= 0);
  TEST_CHECK(close(output_fd) == 0);
  TEST_CHECK(unlink(output_seed) == 0);

  TEST_CHECK(snprintf(output_path, sizeof(output_path), "%s%s", output_seed, ext) > 0);
  TEST_CHECK(snprintf(page1_path, sizeof(page1_path), "%s_page0001%s", output_seed, ext) > 0);
  TEST_CHECK(snprintf(page2_path, sizeof(page2_path), "%s_page0002%s", output_seed, ext) > 0);

  options.input_path = input_template;
  options.output_path = output_path;
  options.auto_order = 1;
  options.paginate = 1;

  TEST_CHECK(hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(result.order == 12u);
  TEST_CHECK(result.side == 4096u);
  TEST_CHECK(result.capacity == (uint64_t)(1u << 24u));
  TEST_CHECK(result.input_bytes == (uint64_t)large_size);
  TEST_CHECK(result.page_count == 2u);
  TEST_CHECK(stat(page1_path, &out_stat) == 0);
  TEST_CHECK(out_stat.st_size > 0);
  TEST_CHECK(stat(page2_path, &out_stat) == 0);
  TEST_CHECK(out_stat.st_size > 0);

  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(page1_path) == 0);
  TEST_CHECK(unlink(page2_path) == 0);
}

static void test_render_empty_input_succeeds(void)
{
  char input_template[] = "/tmp/hv_empty_input_XXXXXX";
  char output_template[] = "/tmp/hv_empty_output_XXXXXX";
  int input_fd = -1;
  int output_fd = -1;
  HvRenderOptions options;
  HvRenderResult result;
  char err[256];
  struct stat out_stat;

  memset(&options, 0, sizeof(options));
  memset(&result, 0, sizeof(result));
  memset(err, 0, sizeof(err));

  input_fd = mkstemp(input_template);
  TEST_CHECK(input_fd >= 0);
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
  TEST_CHECK(result.capacity == 4u);
  TEST_CHECK(result.input_bytes == 0u);
  TEST_CHECK(result.page_count == 1u);

  TEST_CHECK(stat(output_template, &out_stat) == 0);
  TEST_CHECK((uint64_t)out_stat.st_size == 23u);

  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(output_template) == 0);
}

static void test_render_rect_hilbert_strict_rejects_flipped_parity(void)
{
  char input_template[] = "/tmp/hv_rect_strict_flip_input_XXXXXX";
  char output_template[] = "/tmp/hv_rect_strict_flip_output_XXXXXX";
  uint8_t payload[] = {0x14u, 0x15u, 0x16u, 0x17u};
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
  options.layout = HV_LAYOUT_RECT_HILBERT;
  options.dimensions_set = 1;
  options.width = 2u;  /* smaller even */
  options.height = 3u; /* larger odd */
  options.strict_adjacency = 1;

  TEST_CHECK(!hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(strstr(err, "strict adjacency rejects") != 0);

  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(output_template) == 0);
}

static void test_render_rejects_invalid_arguments(void)
{
  HvRenderOptions options;
  HvRenderResult result;
  char err[256];

  memset(&options, 0, sizeof(options));
  memset(&result, 0, sizeof(result));
  memset(err, 0, sizeof(err));

  options.input_path = "/tmp/hv_invalid_render_input";
  options.output_path = "/tmp/hv_invalid_render_output";

  TEST_CHECK(!hv_render_file(0, &result, err, sizeof(err)));
  TEST_CHECK(strstr(err, "invalid arguments for render") != 0);

  memset(err, 0, sizeof(err));
  TEST_CHECK(!hv_render_file(&options, 0, err, sizeof(err)));
  TEST_CHECK(strstr(err, "invalid arguments for render") != 0);
}

static void test_render_rejects_unknown_layout(void)
{
  char input_template[] = "/tmp/hv_unknown_layout_input_XXXXXX";
  char output_template[] = "/tmp/hv_unknown_layout_output_XXXXXX";
  uint8_t payload[] = {0x00u, 0x01u, 0x02u, 0x03u};
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
  options.auto_order = 1;
  options.layout = 99;

  TEST_CHECK(!hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(strstr(err, "unknown layout mode") != 0);

  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(output_template) == 0);
}

static void test_render_rejects_rect_without_dimensions(void)
{
  char input_template[] = "/tmp/hv_rect_missing_dims_input_XXXXXX";
  char output_template[] = "/tmp/hv_rect_missing_dims_output_XXXXXX";
  uint8_t payload[] = {0x10u, 0x11u, 0x12u, 0x13u};
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
  options.layout = HV_LAYOUT_RECT_HILBERT;

  TEST_CHECK(!hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(strstr(err, "requires explicit dimensions") != 0);

  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(output_template) == 0);
}

static void test_render_rejects_rect_zero_dimensions(void)
{
  char input_template[] = "/tmp/hv_rect_zero_dims_input_XXXXXX";
  char output_template[] = "/tmp/hv_rect_zero_dims_output_XXXXXX";
  uint8_t payload[] = {0x21u, 0x22u, 0x23u, 0x24u};
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
  options.layout = HV_LAYOUT_RECT_HILBERT;
  options.dimensions_set = 1;
  options.width = 0u;
  options.height = 2u;

  TEST_CHECK(!hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(strstr(err, "dimensions must be positive") != 0);

  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(output_template) == 0);
}

static void test_render_rejects_hilbert_dimensions(void)
{
  char input_template[] = "/tmp/hv_hilbert_dims_input_XXXXXX";
  char output_template[] = "/tmp/hv_hilbert_dims_output_XXXXXX";
  uint8_t payload[] = {0x31u, 0x32u, 0x33u, 0x34u};
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
  options.auto_order = 1;
  options.layout = HV_LAYOUT_HILBERT;
  options.dimensions_set = 1;
  options.width = 2u;
  options.height = 2u;

  TEST_CHECK(!hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(strstr(err, "dimensions are only supported") != 0);

  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(output_template) == 0);
}

static void test_render_rejects_invalid_manual_order(void)
{
  char input_template[] = "/tmp/hv_bad_order_input_XXXXXX";
  char output_template[] = "/tmp/hv_bad_order_output_XXXXXX";
  uint8_t payload[] = {0x40u, 0x41u, 0x42u, 0x43u};
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
  options.auto_order = 0;
  options.order = 0u;

  TEST_CHECK(!hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(strstr(err, "invalid order 0") != 0);

  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(output_template) == 0);
}

static void test_render_rejects_input_exceeds_capacity_without_paginate(void)
{
  char input_template[] = "/tmp/hv_no_paginate_input_XXXXXX";
  char output_template[] = "/tmp/hv_no_paginate_output_XXXXXX";
  uint8_t payload[] = {0x50u, 0x51u, 0x52u, 0x53u, 0x54u};
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
  options.auto_order = 0;
  options.order = 1u;

  TEST_CHECK(!hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(strstr(err, "exceeds selected capacity") != 0);

  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(output_template) == 0);
}

static void test_render_rejects_invalid_max_image_cap_env(void)
{
  char input_template[] = "/tmp/hv_bad_cap_input_XXXXXX";
  char output_template[] = "/tmp/hv_bad_cap_output_XXXXXX";
  uint8_t payload[] = {0x60u, 0x61u, 0x62u, 0x63u};
  char *saved_cap = 0;
  int input_fd = -1;
  int output_fd = -1;
  ssize_t wrote = 0;
  HvRenderOptions options;
  HvRenderResult result;
  char err[256];

  memset(&options, 0, sizeof(options));
  memset(&result, 0, sizeof(result));
  memset(err, 0, sizeof(err));
  saved_cap = test_save_env("HILBERTVIZ_MAX_IMAGE_BYTES");

  input_fd = mkstemp(input_template);
  TEST_CHECK(input_fd >= 0);
  wrote = write(input_fd, payload, sizeof(payload));
  TEST_CHECK(wrote == (ssize_t)sizeof(payload));
  TEST_CHECK(close(input_fd) == 0);

  output_fd = mkstemp(output_template);
  TEST_CHECK(output_fd >= 0);
  TEST_CHECK(close(output_fd) == 0);

  TEST_CHECK(setenv("HILBERTVIZ_MAX_IMAGE_BYTES", "bogus", 1) == 0);

  options.input_path = input_template;
  options.output_path = output_template;
  options.auto_order = 1;

  TEST_CHECK(!hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(strstr(err, "invalid HILBERTVIZ_MAX_IMAGE_BYTES='bogus'") != 0);

  test_restore_env("HILBERTVIZ_MAX_IMAGE_BYTES", saved_cap);
  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(output_template) == 0);
}

static void test_render_rejects_legend_without_path(void)
{
  char input_template[] = "/tmp/hv_missing_legend_input_XXXXXX";
  char output_template[] = "/tmp/hv_missing_legend_output_XXXXXX";
  uint8_t payload[] = {0x70u, 0x71u, 0x72u, 0x73u};
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
  options.auto_order = 1;
  options.legend_enabled = 1;

  TEST_CHECK(!hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(strstr(err, "legend was enabled but no legend path was provided") != 0);

  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(output_template) == 0);
}

static void test_render_rect_legend_with_explicit_length(void)
{
  char input_template[] = "/tmp/hv_rect_legend_input_XXXXXX";
  char output_template[] = "/tmp/hv_rect_legend_output_XXXXXX";
  char legend_path[160];
  uint8_t payload[] = {0x00u, 0x11u, 0x22u, 0x7Fu, 0x80u, 0xFFu};
  int input_fd = -1;
  int output_fd = -1;
  ssize_t wrote = 0;
  HvRenderOptions options;
  HvRenderResult result;
  char err[256];
  FILE *legend_fp = 0;
  char legend_buf[1024];
  size_t legend_n = 0u;

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

  TEST_CHECK(snprintf(legend_path, sizeof(legend_path), "%s.legend.txt", output_template) > 0);

  options.input_path = input_template;
  options.output_path = output_template;
  options.legend_enabled = 1;
  options.legend_path = legend_path;
  options.layout = HV_LAYOUT_RECT_HILBERT;
  options.dimensions_set = 1;
  options.width = 3u;
  options.height = 2u;
  options.offset = 1u;
  options.has_length = 1;
  options.length = 4u;

  TEST_CHECK(hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(result.page_count == 1u);
  TEST_CHECK(result.capacity == 6u);
  TEST_CHECK(result.input_bytes == 4u);

  legend_fp = fopen(legend_path, "rb");
  TEST_CHECK(legend_fp != 0);
  legend_n = fread(legend_buf, 1u, sizeof(legend_buf) - 1u, legend_fp);
  TEST_CHECK(legend_n > 0u);
  legend_buf[legend_n] = '\0';
  TEST_CHECK(strstr(legend_buf, "layout=rect-hilbert") != 0);
  TEST_CHECK(strstr(legend_buf, "length=explicit") != 0);
  TEST_CHECK(strstr(legend_buf, "order=n/a") != 0);
  TEST_CHECK(fclose(legend_fp) == 0);

  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(output_template) == 0);
  TEST_CHECK(unlink(legend_path) == 0);
}

static void test_render_rejects_directory_output_target(void)
{
  char input_template[] = "/tmp/hv_output_dir_input_XXXXXX";
  char output_dir_template[] = "/tmp/hv_output_dir_target_XXXXXX";
  uint8_t payload[] = {0x80u, 0x81u, 0x82u, 0x83u};
  char *output_dir = 0;
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

  output_dir = mkdtemp(output_dir_template);
  TEST_CHECK(output_dir != 0);

  options.input_path = input_template;
  options.output_path = output_dir;
  options.auto_order = 1;

  TEST_CHECK(!hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(strstr(err, "unsupported output page target type") != 0);

  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(rmdir(output_dir) == 0);
}

static void test_render_rejects_symlink_output_target(void)
{
  char input_template[] = "/tmp/hv_symlink_input_XXXXXX";
  char target_template[] = "/tmp/hv_symlink_target_XXXXXX";
  char link_template[] = "/tmp/hv_symlink_output_XXXXXX";
  uint8_t payload[] = {0x90u, 0x91u, 0x92u, 0x93u};
  int input_fd = -1;
  int target_fd = -1;
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

  target_fd = mkstemp(target_template);
  TEST_CHECK(target_fd >= 0);
  TEST_CHECK(close(target_fd) == 0);

  seed_fd = mkstemp(link_template);
  TEST_CHECK(seed_fd >= 0);
  TEST_CHECK(close(seed_fd) == 0);
  TEST_CHECK(unlink(link_template) == 0);
  TEST_CHECK(symlink(target_template, link_template) == 0);

  options.input_path = input_template;
  options.output_path = link_template;
  options.auto_order = 1;

  TEST_CHECK(!hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(strstr(err, "refusing symlink output page path") != 0);

  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(target_template) == 0);
  TEST_CHECK(unlink(link_template) == 0);
}

static void test_render_rejects_legend_directory_target(void)
{
  char input_template[] = "/tmp/hv_legend_dir_input_XXXXXX";
  char output_template[] = "/tmp/hv_legend_dir_output_XXXXXX";
  char legend_dir_template[] = "/tmp/hv_legend_dir_target_XXXXXX";
  uint8_t payload[] = {0x12u, 0x34u, 0x56u, 0x78u};
  char *legend_dir = 0;
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

  legend_dir = mkdtemp(legend_dir_template);
  TEST_CHECK(legend_dir != 0);

  options.input_path = input_template;
  options.output_path = output_template;
  options.legend_enabled = 1;
  options.legend_path = legend_dir;
  options.auto_order = 1;

  TEST_CHECK(!hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(strstr(err, "unsupported legend target type") != 0);

  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(output_template) == 0);
  TEST_CHECK(rmdir(legend_dir) == 0);
}

static void test_render_page_write_failure_closes_legend(void)
{
  char input_template[] = "/tmp/hv_bad_ext_input_XXXXXX";
  char output_seed[] = "/tmp/hv_bad_ext_output_XXXXXX";
  char output_path[160];
  char legend_path[160];
  uint8_t payload[] = {0x01u, 0x23u, 0x45u, 0x67u};
  int input_fd = -1;
  int output_fd = -1;
  ssize_t wrote = 0;
  HvRenderOptions options;
  HvRenderResult result;
  char err[256];
  FILE *legend_fp = 0;

  memset(&options, 0, sizeof(options));
  memset(&result, 0, sizeof(result));
  memset(err, 0, sizeof(err));

  input_fd = mkstemp(input_template);
  TEST_CHECK(input_fd >= 0);
  wrote = write(input_fd, payload, sizeof(payload));
  TEST_CHECK(wrote == (ssize_t)sizeof(payload));
  TEST_CHECK(close(input_fd) == 0);

  output_fd = mkstemp(output_seed);
  TEST_CHECK(output_fd >= 0);
  TEST_CHECK(close(output_fd) == 0);
  TEST_CHECK(unlink(output_seed) == 0);

  TEST_CHECK(snprintf(output_path, sizeof(output_path), "%s.bad", output_seed) > 0);
  TEST_CHECK(snprintf(legend_path, sizeof(legend_path), "%s.legend.txt", output_seed) > 0);

  options.input_path = input_template;
  options.output_path = output_path;
  options.legend_enabled = 1;
  options.legend_path = legend_path;
  options.auto_order = 1;

  TEST_CHECK(!hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(strstr(err, "unsupported output extension") != 0);

  legend_fp = fopen(legend_path, "rb");
  TEST_CHECK(legend_fp != 0);
  TEST_CHECK(fclose(legend_fp) == 0);

  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(output_path) == 0);
  TEST_CHECK(unlink(legend_path) == 0);
}

static void test_render_page_open_failure_closes_legend(void)
{
  char input_template[] = "/tmp/hv_missing_dir_input_XXXXXX";
  char output_seed[] = "/tmp/hv_missing_dir_seed_XXXXXX";
  char output_path[224];
  char legend_path[160];
  uint8_t payload[] = {0x09u, 0x08u, 0x07u, 0x06u};
  int input_fd = -1;
  int output_fd = -1;
  ssize_t wrote = 0;
  HvRenderOptions options;
  HvRenderResult result;
  char err[256];
  FILE *legend_fp = 0;

  memset(&options, 0, sizeof(options));
  memset(&result, 0, sizeof(result));
  memset(err, 0, sizeof(err));

  input_fd = mkstemp(input_template);
  TEST_CHECK(input_fd >= 0);
  wrote = write(input_fd, payload, sizeof(payload));
  TEST_CHECK(wrote == (ssize_t)sizeof(payload));
  TEST_CHECK(close(input_fd) == 0);

  output_fd = mkstemp(output_seed);
  TEST_CHECK(output_fd >= 0);
  TEST_CHECK(close(output_fd) == 0);
  TEST_CHECK(unlink(output_seed) == 0);

  TEST_CHECK(snprintf(output_path, sizeof(output_path), "%s/missing/out.ppm", output_seed) > 0);
  TEST_CHECK(snprintf(legend_path, sizeof(legend_path), "%s.legend.txt", output_seed) > 0);

  options.input_path = input_template;
  options.output_path = output_path;
  options.legend_enabled = 1;
  options.legend_path = legend_path;
  options.auto_order = 1;

  TEST_CHECK(!hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(strstr(err, "failed to open output page") != 0);

  legend_fp = fopen(legend_path, "rb");
  TEST_CHECK(legend_fp != 0);
  TEST_CHECK(fclose(legend_fp) == 0);

  TEST_CHECK(unlink(input_template) == 0);
  TEST_CHECK(unlink(legend_path) == 0);
}

static void test_ppm_stream_rejects_size_overflow(void)
{
  FILE *fp = 0;
  uint8_t pixel[3] = {0u, 0u, 0u};
  char err[256];

  memset(err, 0, sizeof(err));
  fp = tmpfile();
  TEST_CHECK(fp != 0);
  TEST_CHECK(
    !hv_write_image_stream("overflow.ppm", fp, pixel, UINT32_MAX, UINT32_MAX, err, sizeof(err))
  );
  TEST_CHECK(strstr(err, "image too large") != 0);
  TEST_CHECK(fclose(fp) == 0);
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

static void test_file_io_additional_error_paths(void)
{
  char input_template[] = "/tmp/hv_fileio_extra_XXXXXX";
  char missing_template[] = "/tmp/hv_fileio_missing_XXXXXX";
  uint8_t payload[] = {0xC0u, 0xC1u, 0xC2u, 0xC3u};
  int input_fd = -1;
  int missing_fd = -1;
  ssize_t wrote = 0;
  HvBuffer slice;
  HvInputStream stream;
  uint8_t byte = 0u;
  uint8_t tiny[3] = {0u, 0u, 0u};
  char err[256];

  memset(&slice, 0, sizeof(slice));
  memset(&stream, 0, sizeof(stream));
  memset(err, 0, sizeof(err));

  missing_fd = mkstemp(missing_template);
  TEST_CHECK(missing_fd >= 0);
  TEST_CHECK(close(missing_fd) == 0);
  TEST_CHECK(unlink(missing_template) == 0);

  TEST_CHECK(!hv_read_file_slice(0, 0u, 0, 0u, &slice, err, sizeof(err)));
  TEST_CHECK(strstr(err, "invalid arguments for file read") != 0);
  TEST_CHECK(!hv_read_file_slice(0, 0u, 0, 0u, &slice, 0, 0u));
  TEST_CHECK(!hv_read_file_slice(missing_template, 0u, 0, 0u, &slice, err, sizeof(err)));
  TEST_CHECK(strstr(err, "failed to open input") != 0);

  TEST_CHECK(!hv_open_file_slice_stream(0, 0u, 0, 0u, &stream, err, sizeof(err)));
  TEST_CHECK(strstr(err, "invalid arguments for stream open") != 0);
  TEST_CHECK(!hv_open_file_slice_stream(missing_template, 0u, 0, 0u, 0, err, sizeof(err)));
  TEST_CHECK(strstr(err, "invalid arguments for stream open") != 0);
  TEST_CHECK(!hv_open_file_slice_stream(missing_template, 0u, 0, 0u, &stream, err, sizeof(err)));
  TEST_CHECK(strstr(err, "failed to open input") != 0);

  input_fd = mkstemp(input_template);
  TEST_CHECK(input_fd >= 0);
  wrote = write(input_fd, payload, sizeof(payload));
  TEST_CHECK(wrote == (ssize_t)sizeof(payload));
  TEST_CHECK(close(input_fd) == 0);

  memset(err, 0, sizeof(err));
  TEST_CHECK(!hv_read_file_slice(input_template, 3u, 1, 3u, &slice, err, sizeof(err)));
  TEST_CHECK(strstr(err, "outside file size") != 0);

  memset(err, 0, sizeof(err));
  TEST_CHECK(hv_read_file_slice(input_template, (uint64_t)sizeof(payload), 1, 0u, &slice, err, sizeof(err)));
  TEST_CHECK(slice.data == 0);
  TEST_CHECK(slice.size == 0u);

  memset(err, 0, sizeof(err));
  TEST_CHECK(!hv_stream_read_exact(0, &byte, 1u, err, sizeof(err)));
  TEST_CHECK(strstr(err, "invalid arguments for stream read") != 0);
  TEST_CHECK(!hv_stream_read_exact(&stream, &byte, 1u, err, sizeof(err)));
  TEST_CHECK(strstr(err, "invalid arguments for stream read") != 0);

  memset(err, 0, sizeof(err));
  TEST_CHECK(hv_open_file_slice_stream(input_template, 0u, 1, 1u, &stream, err, sizeof(err)));
  TEST_CHECK(!hv_stream_read_exact(&stream, 0, 1u, err, sizeof(err)));
  TEST_CHECK(strstr(err, "invalid arguments for stream read") != 0);
  TEST_CHECK(hv_close_input_stream(&stream, err, sizeof(err)));

  memset(err, 0, sizeof(err));
  TEST_CHECK(hv_open_file_slice_stream(input_template, 1u, 1, 2u, &stream, err, sizeof(err)));
  TEST_CHECK(!hv_stream_read_exact(&stream, tiny, sizeof(tiny), err, sizeof(err)));
  TEST_CHECK(strstr(err, "exceeds remaining slice") != 0);
  TEST_CHECK(hv_close_input_stream(&stream, err, sizeof(err)));

  TEST_CHECK(!hv_close_input_stream(0, err, sizeof(err)));
  TEST_CHECK(strstr(err, "invalid arguments for stream close") != 0);

  stream.fp = 0;
  stream.remaining = 7u;
  stream.total = 7u;
  TEST_CHECK(hv_close_input_stream(&stream, err, sizeof(err)));
  TEST_CHECK(stream.remaining == 0u);
  TEST_CHECK(stream.total == 0u);

  hv_free_buffer(0);
  slice.data = (uint8_t *)malloc(1u);
  TEST_CHECK(slice.data != 0);
  slice.size = 1u;
  hv_free_buffer(&slice);
  TEST_CHECK(slice.data == 0);
  TEST_CHECK(slice.size == 0u);

  TEST_CHECK(unlink(input_template) == 0);
}

static void test_file_io_respects_max_slice_cap(void)
{
  char input_template[] = "/tmp/hv_slice_cap_input_XXXXXX";
  char *saved_cap = 0;
  HvBuffer slice;
  int input_fd = -1;
  const off_t large_size = (off_t)(2u * 1024u * 1024u);
  char err[256];

  memset(&slice, 0, sizeof(slice));
  memset(err, 0, sizeof(err));
  saved_cap = test_save_env("HILBERTVIZ_MAX_SLICE_BYTES");

  input_fd = mkstemp(input_template);
  TEST_CHECK(input_fd >= 0);
  TEST_CHECK(ftruncate(input_fd, large_size) == 0);
  TEST_CHECK(close(input_fd) == 0);

  TEST_CHECK(setenv("HILBERTVIZ_MAX_SLICE_BYTES", "1048576", 1) == 0);
  TEST_CHECK(!hv_read_file_slice(input_template, 0u, 0, 0u, &slice, err, sizeof(err)));
  TEST_CHECK(strstr(err, "exceeds configured cap") != 0);
  TEST_CHECK(slice.data == 0);
  TEST_CHECK(slice.size == 0u);

  memset(err, 0, sizeof(err));
  TEST_CHECK(setenv("HILBERTVIZ_MAX_SLICE_BYTES", "0", 1) == 0);
  TEST_CHECK(hv_read_file_slice(input_template, 0u, 0, 0u, &slice, err, sizeof(err)));
  TEST_CHECK(slice.size == (size_t)large_size);
  TEST_CHECK(slice.data != 0);
  TEST_CHECK(slice.data[0] == 0u);
  TEST_CHECK(slice.data[slice.size - 1u] == 0u);

  hv_free_buffer(&slice);
  test_restore_env("HILBERTVIZ_MAX_SLICE_BYTES", saved_cap);
  TEST_CHECK(unlink(input_template) == 0);
}

static void test_file_io_rejects_invalid_max_slice_cap_env(void)
{
  char input_template[] = "/tmp/hv_bad_slice_env_input_XXXXXX";
  char *saved_cap = 0;
  uint8_t payload[] = {0xA1u, 0xA2u, 0xA3u, 0xA4u};
  HvBuffer slice;
  int input_fd = -1;
  ssize_t wrote = 0;
  char err[256];

  memset(&slice, 0, sizeof(slice));
  memset(err, 0, sizeof(err));
  saved_cap = test_save_env("HILBERTVIZ_MAX_SLICE_BYTES");

  input_fd = mkstemp(input_template);
  TEST_CHECK(input_fd >= 0);
  wrote = write(input_fd, payload, sizeof(payload));
  TEST_CHECK(wrote == (ssize_t)sizeof(payload));
  TEST_CHECK(close(input_fd) == 0);

  TEST_CHECK(setenv("HILBERTVIZ_MAX_SLICE_BYTES", "oops", 1) == 0);
  TEST_CHECK(!hv_read_file_slice(input_template, 0u, 0, 0u, &slice, err, sizeof(err)));
  TEST_CHECK(strstr(err, "invalid HILBERTVIZ_MAX_SLICE_BYTES='oops'") != 0);
  TEST_CHECK(slice.data == 0);
  TEST_CHECK(slice.size == 0u);

  test_restore_env("HILBERTVIZ_MAX_SLICE_BYTES", saved_cap);
  TEST_CHECK(unlink(input_template) == 0);
}

static void test_file_io_zero_length_slice_succeeds(void)
{
  char input_template[] = "/tmp/hv_zero_slice_input_XXXXXX";
  uint8_t payload[] = {0xB1u, 0xB2u, 0xB3u, 0xB4u};
  HvBuffer slice;
  int input_fd = -1;
  ssize_t wrote = 0;
  char err[256];

  memset(&slice, 0, sizeof(slice));
  memset(err, 0, sizeof(err));

  input_fd = mkstemp(input_template);
  TEST_CHECK(input_fd >= 0);
  wrote = write(input_fd, payload, sizeof(payload));
  TEST_CHECK(wrote == (ssize_t)sizeof(payload));
  TEST_CHECK(close(input_fd) == 0);

  TEST_CHECK(hv_read_file_slice(input_template, 2u, 1, 0u, &slice, err, sizeof(err)));
  TEST_CHECK(slice.data == 0);
  TEST_CHECK(slice.size == 0u);

  hv_free_buffer(&slice);
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
  TEST_CHECK(
    (strstr(err, "refusing destructive path alias") != 0) ||
    (strstr(err, "refusing symlink output page path") != 0)
  );
  TEST_CHECK(
    (strstr(err, "aliases input") != 0) ||
    (strstr(err, "symlink output page path") != 0)
  );

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

static void test_alias_race_generated_page_swapped_to_legend_rejected(void)
{
  char base_template[] = "/tmp/hv_alias_legend_race_XXXXXX";
  char input_path[192];
  char output_base[192];
  char legend_path[192];
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
  TEST_CHECK(snprintf(legend_path, sizeof(legend_path), "%s.legend.txt", base_template) > 0);
  TEST_CHECK(snprintf(page1_path, sizeof(page1_path), "%s_page0001.ppm", base_template) > 0);
  TEST_CHECK(snprintf(page2_path, sizeof(page2_path), "%s_page0002.ppm", base_template) > 0);

  input_fd = open(input_path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
  TEST_CHECK(input_fd >= 0);
  TEST_CHECK(test_write_all(input_fd, payload, payload_size));
  TEST_CHECK(close(input_fd) == 0);

  helper_pid = fork();
  TEST_CHECK(helper_pid >= 0);
  if (helper_pid == 0) {
    int ready = 0;
    int tries = 0;
    const struct timespec delay = {0, 1000000L};

    for (tries = 0; tries < 20000; ++tries) {
      if ((access(page1_path, F_OK) == 0) && (access(legend_path, F_OK) == 0)) {
        ready = 1;
        break;
      }
      (void)nanosleep(&delay, 0);
    }
    if (!ready) {
      _exit(2);
    }

    (void)unlink(page2_path);
    if (link(legend_path, page2_path) != 0) {
      _exit(3);
    }
    _exit(0);
  }

  options.input_path = input_path;
  options.output_path = output_base;
  options.legend_enabled = 1;
  options.legend_path = legend_path;
  options.auto_order = 0;
  options.order = 9u;
  options.paginate = 1;

  TEST_CHECK(!hv_render_file(&options, &result, err, sizeof(err)));
  TEST_CHECK(strstr(err, "refusing destructive path alias") != 0);
  TEST_CHECK(strstr(err, "aliases open legend path") != 0);

  TEST_CHECK(waitpid(helper_pid, &helper_status, 0) == helper_pid);
  TEST_CHECK(WIFEXITED(helper_status));
  TEST_CHECK(WEXITSTATUS(helper_status) == 0);

  test_assert_file_content(input_path, payload, payload_size);

  free(payload);
  payload = 0;
  TEST_CHECK(unlink(input_path) == 0);
  (void)unlink(legend_path);
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
  test_gilbert_rect_bijection();
  test_gilbert_depth_limit_and_skinny_extremes();
  test_hilbert3d_order_helpers();
  test_hilbert3d_d2xyz_order1();
  test_hilbert3d_bijection_and_adjacency();
  test_3d_camera_defaults();
  test_3d_camera_fit_cloud_small();
  test_3d_camera_fit_cloud_large();
  test_3d_renderer_summary_output();
  test_3d_renderer_invalid_arguments();
  test_3d_platform_invalid_arguments();
  test_point_cloud3d_empty_slice();
  test_point_cloud3d_small_deterministic();
  test_point_cloud3d_near_capacity_slice();
  test_point_cloud3d_offset_and_length();
  test_point_cloud3d_invalid_arguments();
  test_palette_edges();
  test_image_api_dispatch_and_errors();
  test_ppm_writer_wrapper_and_error_paths();
  test_png_writer_wrapper_and_error_paths();
  test_image_writers_null_error_buffer_paths();
  test_render_integration();
  test_render_paginate_and_legend();
  test_render_rect_hilbert_integration();
  test_render_rect_hilbert_strict_rejects_parity();
  test_render_rect_hilbert_strict_rejects_flipped_parity();
  test_render_respects_max_image_cap();
  test_render_output_char_device_succeeds();
  test_entropy_single_symbol_zero();
  test_entropy_balanced_two_symbol_slice();
  test_render_entropy_and_legend_key();
  test_render_missing_input_reports_open_error();
  test_render_auto_paginate_small_uses_auto_order();
  test_render_auto_paginate_large_uses_default_page_order();
  test_render_empty_input_succeeds();
  test_render_rejects_invalid_arguments();
  test_render_rejects_unknown_layout();
  test_render_rejects_rect_without_dimensions();
  test_render_rejects_rect_zero_dimensions();
  test_render_rejects_hilbert_dimensions();
  test_render_rejects_invalid_manual_order();
  test_render_rejects_input_exceeds_capacity_without_paginate();
  test_render_rejects_invalid_max_image_cap_env();
  test_render_rejects_legend_without_path();
  test_render_rect_legend_with_explicit_length();
  test_render_rejects_directory_output_target();
  test_render_rejects_symlink_output_target();
  test_render_rejects_legend_directory_target();
  test_render_page_write_failure_closes_legend();
  test_render_page_open_failure_closes_legend();
  test_ppm_stream_rejects_size_overflow();
  test_file_io_slice_and_stream_semantics();
  test_file_io_stream_detects_truncate_race();
  test_file_io_additional_error_paths();
  test_file_io_respects_max_slice_cap();
  test_file_io_rejects_invalid_max_slice_cap_env();
  test_file_io_zero_length_slice_succeeds();
  test_alias_legend_equals_input_rejected();
  test_alias_output_equals_input_rejected();
  test_alias_legend_equals_output_rejected();
  test_alias_generated_page_equals_input_rejected();
  test_alias_symlink_output_to_input_rejected();
  test_alias_legend_equals_generated_page_rejected();
  test_alias_hardlink_output_to_input_rejected();
  test_alias_race_generated_page_swapped_to_input_rejected();
  test_alias_race_generated_page_swapped_to_legend_rejected();
#ifdef HV_TEST_HAVE_PNG
  test_render_png_output();
  test_png_stream_rejects_size_overflow();
#endif
  return 0;
}
