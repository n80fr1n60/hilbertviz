#include "point_cloud3d.h"

#include "file_io.h"
#include "hilbert3d.h"
#include "palette.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HV_POINT_CLOUD3D_READ_CHUNK_BYTES 65536u

#if defined(__GNUC__) || defined(__clang__)
#define HV_PRINTF_LIKE(fmt_idx, first_arg_idx) __attribute__((format(printf, fmt_idx, first_arg_idx)))
#else
#define HV_PRINTF_LIKE(fmt_idx, first_arg_idx)
#endif

static void HV_PRINTF_LIKE(3, 4) hv_set_error(char *err, size_t err_size, const char *fmt, ...)
{
  va_list args;

  if ((err == 0) || (err_size == 0u)) {
    return;
  }

  va_start(args, fmt);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
  (void)vsnprintf(err, err_size, fmt, args);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
  va_end(args);
}

static int hv_mul_size(size_t a, size_t b, size_t *out)
{
  if (out == 0) {
    return 0;
  }
  if ((a != 0u) && (b > (SIZE_MAX / a))) {
    return 0;
  }
  *out = a * b;
  return 1;
}

static float hv_normalize_axis(uint32_t coord, uint32_t side)
{
  return ((((float)coord + 0.5f) / (float)side) * 2.0f) - 1.0f;
}

static void hv_point_cloud3d_reset(HvPointCloud3D *cloud)
{
  if (cloud == 0) {
    return;
  }
  cloud->points = 0;
  cloud->count = 0u;
  cloud->order = 0u;
  cloud->side = 0u;
  cloud->capacity = 0u;
}

void hv_free_point_cloud3d(HvPointCloud3D *cloud)
{
  if (cloud == 0) {
    return;
  }
  free(cloud->points);
  hv_point_cloud3d_reset(cloud);
}

int hv_build_point_cloud3d(
  const char *input_path,
  uint32_t order,
  uint64_t offset,
  int has_length,
  uint64_t length,
  HvPointCloud3D *cloud_out,
  char *err,
  size_t err_size
)
{
  HvInputStream stream;
  HvPoint3D *points = 0;
  uint8_t read_buf[HV_POINT_CLOUD3D_READ_CHUNK_BYTES];
  uint64_t capacity = 0u;
  uint32_t side = 0u;
  size_t point_count = 0u;
  size_t alloc_size = 0u;
  size_t total_written = 0u;

  if ((input_path == 0) || (cloud_out == 0)) {
    hv_set_error(err, err_size, "invalid arguments for 3D point cloud build");
    return 0;
  }

  hv_point_cloud3d_reset(cloud_out);
  memset(&stream, 0, sizeof(stream));

  if (!hv_hilbert3d_side_for_order(order, &side) || !hv_hilbert3d_capacity_for_order(order, &capacity)) {
    hv_set_error(
      err,
      err_size,
      "invalid 3D order %u (supported range: %u..%u)",
      order,
      HV_HILBERT3D_MIN_ORDER,
      HV_HILBERT3D_MAX_ORDER
    );
    return 0;
  }

  if (!hv_open_file_slice_stream(input_path, offset, has_length, length, &stream, err, err_size)) {
    return 0;
  }

  if (stream.total > capacity) {
    hv_set_error(
      err,
      err_size,
      "selected slice (%" PRIu64 " bytes) exceeds 3D order %u capacity (%" PRIu64 " points)",
      stream.total,
      order,
      capacity
    );
    (void)hv_close_input_stream(&stream, 0, 0u);
    return 0;
  }

  point_count = (size_t)stream.total;
  if ((stream.total > 0u) && !hv_mul_size(point_count, sizeof(*points), &alloc_size)) {
    hv_set_error(err, err_size, "3D point cloud allocation overflow");
    (void)hv_close_input_stream(&stream, 0, 0u);
    return 0;
  }

  if (alloc_size > 0u) {
    points = (HvPoint3D *)malloc(alloc_size);
    if (points == 0) {
      hv_set_error(err, err_size, "failed to allocate %zu bytes for 3D point cloud", alloc_size);
      (void)hv_close_input_stream(&stream, 0, 0u);
      return 0;
    }
  }

  while (total_written < point_count) {
    size_t chunk_size = point_count - total_written;
    size_t i = 0u;

    if (chunk_size > sizeof(read_buf)) {
      chunk_size = sizeof(read_buf);
    }

    if (!hv_stream_read_exact(&stream, read_buf, chunk_size, err, err_size)) {
      free(points);
      (void)hv_close_input_stream(&stream, 0, 0u);
      return 0;
    }

    for (i = 0u; i < chunk_size; ++i) {
      uint32_t x = 0u;
      uint32_t y = 0u;
      uint32_t z = 0u;
      uint8_t rgb[3];
      size_t index = total_written + i;

      if (!hv_hilbert3d_d2xyz(order, (uint64_t)index, &x, &y, &z)) {
        hv_set_error(err, err_size, "failed to map 3D Hilbert index %zu", index);
        free(points);
        (void)hv_close_input_stream(&stream, 0, 0u);
        return 0;
      }

      hv_byte_to_rgb(read_buf[i], rgb);
      points[index].x = hv_normalize_axis(x, side);
      points[index].y = hv_normalize_axis(y, side);
      points[index].z = hv_normalize_axis(z, side);
      points[index].r = rgb[0];
      points[index].g = rgb[1];
      points[index].b = rgb[2];
    }

    total_written += chunk_size;
  }

  if (!hv_close_input_stream(&stream, err, err_size)) {
    free(points);
    return 0;
  }

  cloud_out->points = points;
  cloud_out->count = point_count;
  cloud_out->order = order;
  cloud_out->side = side;
  cloud_out->capacity = capacity;
  return 1;
}
