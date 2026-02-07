#include "fuzz_target.h"

#include "file_io.h"
#include "hilbert.h"
#include "render.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define HV_FUZZ_HEADER_SIZE 10u
#define HV_FUZZ_MAX_PAYLOAD (1u << 20u)
#define HV_FUZZ_STREAM_BUF 4096u

static uint32_t hv_u32le(const uint8_t *p)
{
  return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

static uint64_t hv_mod_u64(uint64_t value, uint64_t mod)
{
  if (mod == 0u) {
    return 0u;
  }
  return value % mod;
}

static int hv_write_all(int fd, const uint8_t *buf, size_t size)
{
  size_t total = 0u;

  while (total < size) {
    size_t remaining = size - total;
    ssize_t n = write(fd, buf + total, remaining);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return 0;
    }
    if (n == 0) {
      return 0;
    }
    total += (size_t)n;
  }

  return 1;
}

static int hv_pick_temp_dir(char *dir_out, size_t dir_size)
{
  struct stat st;

  if ((dir_out == 0) || (dir_size < 8u)) {
    return 0;
  }

  if ((stat("/dev/shm", &st) == 0) && S_ISDIR(st.st_mode) && (access("/dev/shm", W_OK | X_OK) == 0)) {
    (void)snprintf(dir_out, dir_size, "/dev/shm");
    return 1;
  }

  (void)snprintf(dir_out, dir_size, "/tmp");
  return 1;
}

static int hv_make_temp_input_file(
  const uint8_t *payload,
  size_t payload_size,
  char path_out[PATH_MAX]
)
{
  char dir[16];
  char templ[PATH_MAX];
  int fd = -1;

  if ((payload == 0) || (path_out == 0)) {
    return 0;
  }

  if (!hv_pick_temp_dir(dir, sizeof(dir))) {
    return 0;
  }

  if (snprintf(templ, sizeof(templ), "%s/%s", dir, "hvfuzz_input_XXXXXX") <= 0) {
    return 0;
  }

  fd = mkstemp(templ);
  if (fd < 0) {
    return 0;
  }

  if ((payload_size > 0u) && !hv_write_all(fd, payload, payload_size)) {
    (void)close(fd);
    (void)unlink(templ);
    return 0;
  }

  if (close(fd) != 0) {
    (void)unlink(templ);
    return 0;
  }

  memcpy(path_out, templ, strlen(templ) + 1u);
  return 1;
}

static void hv_derive_slice(
  const uint8_t *header,
  size_t payload_size,
  uint64_t *offset_out,
  int *has_length_out,
  uint64_t *length_out
)
{
  uint64_t offset = 0u;
  int has_length = 0;
  uint64_t length = 0u;
  uint64_t max_after_offset = 0u;
  uint32_t offset_seed = 0u;
  uint32_t length_seed = 0u;

  offset_seed = hv_u32le(header + 2u);
  length_seed = hv_u32le(header + 6u);

  offset = hv_mod_u64((uint64_t)offset_seed, (uint64_t)payload_size + 1u);
  max_after_offset = (uint64_t)payload_size - offset;

  has_length = ((header[0] & 0x04u) != 0u) ? 1 : 0;
  if (has_length) {
    length = hv_mod_u64((uint64_t)length_seed, max_after_offset + 1u);
  }

  *offset_out = offset;
  *has_length_out = has_length;
  *length_out = length;
}

int hv_fuzz_pipeline(const uint8_t *data, size_t size)
{
  uint8_t header[HV_FUZZ_HEADER_SIZE];
  const uint8_t *payload = 0;
  size_t payload_size = 0u;
  char input_path[PATH_MAX];
  HvRenderOptions options;
  HvRenderResult result;
  uint64_t offset = 0u;
  int has_length = 0;
  uint64_t length = 0u;
  uint8_t flags = 0u;
  uint32_t order_span = HV_HILBERT_MAX_ORDER - HV_HILBERT_MIN_ORDER + 1u;
  uint32_t order = HV_HILBERT_MIN_ORDER;
  char err[256];

  if ((data == 0) || (size < HV_FUZZ_HEADER_SIZE)) {
    return 0;
  }

  memcpy(header, data, HV_FUZZ_HEADER_SIZE);
  payload = data + HV_FUZZ_HEADER_SIZE;
  payload_size = size - HV_FUZZ_HEADER_SIZE;
  if (payload_size > HV_FUZZ_MAX_PAYLOAD) {
    payload_size = HV_FUZZ_MAX_PAYLOAD;
  }

  if (!hv_make_temp_input_file(payload, payload_size, input_path)) {
    return 0;
  }

  hv_derive_slice(header, payload_size, &offset, &has_length, &length);
  flags = header[0];

  order = HV_HILBERT_MIN_ORDER + ((uint32_t)header[1] % order_span);
  if (order > 11u) {
    order = 11u;
  }

  memset(&options, 0, sizeof(options));
  memset(&result, 0, sizeof(result));
  memset(err, 0, sizeof(err));

  options.input_path = input_path;
  options.output_path = "/dev/null";
  options.legend_path = 0;
  options.offset = offset;
  options.has_length = has_length;
  options.length = length;
  options.auto_order = ((flags & 0x01u) != 0u) ? 1 : 0;
  options.order = order;
  options.paginate = 0;
  options.legend_enabled = 0;

  (void)hv_render_file(&options, &result, err, sizeof(err));
  (void)unlink(input_path);
  return 0;
}

int hv_fuzz_file_slice(const uint8_t *data, size_t size)
{
  uint8_t header[HV_FUZZ_HEADER_SIZE];
  const uint8_t *payload = 0;
  size_t payload_size = 0u;
  uint64_t offset = 0u;
  int has_length = 0;
  uint64_t length = 0u;
  char input_path[PATH_MAX];
  HvInputStream stream;
  HvBuffer full_buf;
  uint8_t tmp[HV_FUZZ_STREAM_BUF];
  uint8_t mode = 0u;
  char err[256];

  if ((data == 0) || (size < HV_FUZZ_HEADER_SIZE)) {
    return 0;
  }

  memcpy(header, data, HV_FUZZ_HEADER_SIZE);
  payload = data + HV_FUZZ_HEADER_SIZE;
  payload_size = size - HV_FUZZ_HEADER_SIZE;
  if (payload_size > HV_FUZZ_MAX_PAYLOAD) {
    payload_size = HV_FUZZ_MAX_PAYLOAD;
  }

  if (!hv_make_temp_input_file(payload, payload_size, input_path)) {
    return 0;
  }

  hv_derive_slice(header, payload_size, &offset, &has_length, &length);
  memset(&stream, 0, sizeof(stream));
  memset(&full_buf, 0, sizeof(full_buf));
  memset(err, 0, sizeof(err));

  mode = header[0] & 0x03u;
  if (mode == 0u) {
    if (
      hv_open_file_slice_stream(
        input_path,
        offset,
        has_length,
        length,
        &stream,
        err,
        sizeof(err)
      )
    ) {
      while (stream.remaining > 0u) {
        size_t chunk = HV_FUZZ_STREAM_BUF;
        if (stream.remaining < (uint64_t)chunk) {
          chunk = (size_t)stream.remaining;
        }
        if (!hv_stream_read_exact(&stream, tmp, chunk, err, sizeof(err))) {
          break;
        }
      }
      (void)hv_close_input_stream(&stream, err, sizeof(err));
    }
  } else {
    if (
      hv_read_file_slice(
        input_path,
        offset,
        has_length,
        length,
        &full_buf,
        err,
        sizeof(err)
      )
    ) {
      volatile uint8_t sink = 0u;
      if (full_buf.size > 0u) {
        sink = full_buf.data[full_buf.size - 1u];
      }
      (void)sink;
      hv_free_buffer(&full_buf);
    }
  }

  (void)unlink(input_path);
  return 0;
}
