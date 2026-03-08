#include "byte_cube.h"

#include "file_io.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HV_BYTE_CUBE_READ_CHUNK_BYTES 65536u
#define HV_DEFAULT_MAX_BYTE_CUBE_BYTES HV_BYTE_CUBE_VOLUME_BYTES

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

static int hv_mul_u64(uint64_t a, uint64_t b, uint64_t *out)
{
  if (out == 0) {
    return 0;
  }
  if ((a != 0u) && (b > (UINT64_MAX / a))) {
    return 0;
  }
  *out = a * b;
  return 1;
}

static int hv_parse_u64_decimal_strict(const char *text, uint64_t *out)
{
  const unsigned char *p = 0;
  unsigned long long parsed = 0u;
  char *end = 0;

  if ((text == 0) || (out == 0) || (*text == '\0')) {
    return 0;
  }
  if ((text[0] == '+') || (text[0] == '-')) {
    return 0;
  }

  p = (const unsigned char *)text;
  while (*p != '\0') {
    if ((*p < (unsigned char)'0') || (*p > (unsigned char)'9')) {
      return 0;
    }
    ++p;
  }

  errno = 0;
  parsed = strtoull(text, &end, 10);
  if ((errno != 0) || (end == text) || (*end != '\0')) {
    return 0;
  }

  *out = (uint64_t)parsed;
  return 1;
}

static int hv_resolve_max_byte_cube_bytes(uint64_t *out, char *err, size_t err_size)
{
  const char *env = getenv("HILBERTVIZ_MAX_BYTE_CUBE_BYTES");
  uint64_t parsed = 0u;

  if (out == 0) {
    hv_set_error(err, err_size, "invalid byte-cube cap arguments");
    return 0;
  }

  if ((env == 0) || (*env == '\0')) {
    *out = HV_DEFAULT_MAX_BYTE_CUBE_BYTES;
    return 1;
  }

  if (!hv_parse_u64_decimal_strict(env, &parsed)) {
    hv_set_error(
      err,
      err_size,
      "invalid HILBERTVIZ_MAX_BYTE_CUBE_BYTES='%s' (expected unsigned decimal bytes)",
      env
    );
    return 0;
  }

  *out = parsed;
  return 1;
}

static size_t hv_byte_cube_index(uint8_t x, uint8_t y, uint8_t z)
{
  return ((((size_t)x * (size_t)HV_BYTE_CUBE_SIDE) + (size_t)y) * (size_t)HV_BYTE_CUBE_SIDE) + (size_t)z;
}

static float hv_byte_cube_slice_coord(uint8_t min_coord, uint8_t max_coord)
{
  return ((((float)min_coord + (float)max_coord) * 0.5f) + 0.5f) / (float)HV_BYTE_CUBE_SIDE;
}

static int hv_byte_cube_is_ready(const HvByteCube3D *cube)
{
  return (
    (cube != 0) &&
    (cube->voxels != 0) &&
    (cube->side == HV_BYTE_CUBE_SIDE) &&
    (cube->total_voxels == HV_BYTE_CUBE_TOTAL_VOXELS)
  );
}

static void hv_byte_cube_reset(HvByteCube3D *cube)
{
  if (cube == 0) {
    return;
  }
  cube->voxels = 0;
  cube->side = 0u;
  cube->total_voxels = 0u;
  cube->trigram_count = 0u;
  cube->occupied_voxels = 0u;
  cube->occupied_min_x = 0u;
  cube->occupied_min_y = 0u;
  cube->occupied_min_z = 0u;
  cube->occupied_max_x = 0u;
  cube->occupied_max_y = 0u;
  cube->occupied_max_z = 0u;
  cube->max_density = 0u;
  cube->max_x = 0u;
  cube->max_y = 0u;
  cube->max_z = 0u;
}

void hv_free_byte_cube3d(HvByteCube3D *cube)
{
  if (cube == 0) {
    return;
  }
  free(cube->voxels);
  hv_byte_cube_reset(cube);
}

int hv_byte_cube_increment_trigram(
  HvByteCube3D *cube,
  uint8_t x,
  uint8_t y,
  uint8_t z,
  char *err,
  size_t err_size
)
{
  size_t voxel_index = 0u;
  uint32_t count = 0u;

  if (!hv_byte_cube_is_ready(cube)) {
    hv_set_error(err, err_size, "invalid byte-cube state");
    return 0;
  }

  voxel_index = hv_byte_cube_index(x, y, z);
  count = cube->voxels[voxel_index];

  if (count == 0u) {
    cube->occupied_voxels += 1u;
    if (cube->occupied_voxels == 1u) {
      cube->occupied_min_x = x;
      cube->occupied_min_y = y;
      cube->occupied_min_z = z;
      cube->occupied_max_x = x;
      cube->occupied_max_y = y;
      cube->occupied_max_z = z;
    } else {
      if (x < cube->occupied_min_x) {
        cube->occupied_min_x = x;
      }
      if (y < cube->occupied_min_y) {
        cube->occupied_min_y = y;
      }
      if (z < cube->occupied_min_z) {
        cube->occupied_min_z = z;
      }
      if (x > cube->occupied_max_x) {
        cube->occupied_max_x = x;
      }
      if (y > cube->occupied_max_y) {
        cube->occupied_max_y = y;
      }
      if (z > cube->occupied_max_z) {
        cube->occupied_max_z = z;
      }
    }
  }
  if (count != UINT32_MAX) {
    count += 1u;
    cube->voxels[voxel_index] = count;
  }
  if (count > cube->max_density) {
    cube->max_density = count;
    cube->max_x = x;
    cube->max_y = y;
    cube->max_z = z;
  }

  return 1;
}

int hv_byte_cube_default_slices(
  const HvByteCube3D *cube,
  float *slice_x,
  float *slice_y,
  float *slice_z,
  char *err,
  size_t err_size
)
{
  if ((cube == 0) || (slice_x == 0) || (slice_y == 0) || (slice_z == 0)) {
    hv_set_error(err, err_size, "invalid arguments for byte-cube default slices");
    return 0;
  }

  if ((cube->side != HV_BYTE_CUBE_SIDE) || (cube->total_voxels != HV_BYTE_CUBE_TOTAL_VOXELS)) {
    hv_set_error(err, err_size, "invalid byte-cube state for default slices");
    return 0;
  }

  if (cube->occupied_voxels == 0u) {
    *slice_x = 0.5f;
    *slice_y = 0.5f;
    *slice_z = 0.5f;
    return 1;
  }

  *slice_x = hv_byte_cube_slice_coord(cube->occupied_min_x, cube->occupied_max_x);
  *slice_y = hv_byte_cube_slice_coord(cube->occupied_min_y, cube->occupied_max_y);
  *slice_z = hv_byte_cube_slice_coord(cube->occupied_min_z, cube->occupied_max_z);
  return 1;
}

int hv_build_byte_cube3d(
  const char *input_path,
  uint64_t offset,
  int has_length,
  uint64_t length,
  HvByteCube3D *cube_out,
  char *err,
  size_t err_size
)
{
  HvInputStream stream;
  uint8_t read_buf[HV_BYTE_CUBE_READ_CHUNK_BYTES];
  uint32_t *voxels = 0;
  size_t alloc_size = 0u;
  size_t total_voxels = (size_t)HV_BYTE_CUBE_TOTAL_VOXELS;
  size_t remaining = 0u;
  uint64_t required_bytes = 0u;
  uint64_t max_byte_cube_bytes = 0u;
  uint8_t prev2 = 0u;
  uint8_t prev1 = 0u;
  int have_prev2 = 0;
  int have_prev1 = 0;

  if ((input_path == 0) || (cube_out == 0)) {
    hv_set_error(err, err_size, "invalid arguments for byte-cube build");
    return 0;
  }

  hv_byte_cube_reset(cube_out);
  memset(&stream, 0, sizeof(stream));

  if (!hv_mul_size(total_voxels, sizeof(*voxels), &alloc_size)) {
    hv_set_error(err, err_size, "byte-cube allocation overflow");
    return 0;
  }
  if (!hv_mul_u64(HV_BYTE_CUBE_TOTAL_VOXELS, (uint64_t)sizeof(*voxels), &required_bytes)) {
    hv_set_error(err, err_size, "byte-cube required-bytes overflow");
    return 0;
  }
  if (!hv_resolve_max_byte_cube_bytes(&max_byte_cube_bytes, err, err_size)) {
    return 0;
  }
  if ((max_byte_cube_bytes != 0u) && (required_bytes > max_byte_cube_bytes)) {
    hv_set_error(
      err,
      err_size,
      "byte-cube volume (%" PRIu64 " bytes) exceeds configured cap (%" PRIu64 " bytes); set HILBERTVIZ_MAX_BYTE_CUBE_BYTES to raise/disable (0) the cap",
      required_bytes,
      max_byte_cube_bytes
    );
    return 0;
  }

  voxels = (uint32_t *)calloc(total_voxels, sizeof(*voxels));
  if (voxels == 0) {
    hv_set_error(err, err_size, "failed to allocate %zu bytes for byte-cube volume", alloc_size);
    return 0;
  }
  cube_out->voxels = voxels;
  cube_out->side = HV_BYTE_CUBE_SIDE;
  cube_out->total_voxels = HV_BYTE_CUBE_TOTAL_VOXELS;

  if (!hv_open_file_slice_stream(input_path, offset, has_length, length, &stream, err, err_size)) {
    hv_free_byte_cube3d(cube_out);
    return 0;
  }
  if (stream.total > (uint64_t)SIZE_MAX) {
    hv_set_error(err, err_size, "byte-cube slice too large for host size_t");
    (void)hv_close_input_stream(&stream, 0, 0u);
    hv_free_byte_cube3d(cube_out);
    return 0;
  }

  remaining = (size_t)stream.total;
  while (remaining > 0u) {
    size_t chunk_size = remaining;
    size_t i = 0u;

    if (chunk_size > sizeof(read_buf)) {
      chunk_size = sizeof(read_buf);
    }

    if (!hv_stream_read_exact(&stream, read_buf, chunk_size, err, err_size)) {
      hv_free_byte_cube3d(cube_out);
      (void)hv_close_input_stream(&stream, 0, 0u);
      return 0;
    }

    for (i = 0u; i < chunk_size; ++i) {
      uint8_t current = read_buf[i];

      if (have_prev2 && have_prev1) {
        if (!hv_byte_cube_increment_trigram(cube_out, prev2, prev1, current, err, err_size)) {
          (void)hv_close_input_stream(&stream, 0, 0u);
          hv_free_byte_cube3d(cube_out);
          return 0;
        }
        cube_out->trigram_count += 1u;
      }

      prev2 = prev1;
      have_prev2 = have_prev1;
      prev1 = current;
      have_prev1 = 1;
    }

    remaining -= chunk_size;
  }

  if (!hv_close_input_stream(&stream, err, err_size)) {
    hv_free_byte_cube3d(cube_out);
    return 0;
  }

  return 1;
}

int hv_write_byte_cube3d_summary(
  FILE *stream,
  const HvByteCube3D *cube,
  char *err,
  size_t err_size
)
{
  int rc = 0;

  if ((stream == 0) || (cube == 0)) {
    hv_set_error(err, err_size, "invalid arguments for byte-cube summary");
    return 0;
  }

  rc = fprintf(
    stream,
    "Loaded byte cube: trigrams=%" PRIu64 " occupied_voxels=%" PRIu64 " side=%u total_voxels=%" PRIu64 "\n",
    cube->trigram_count,
    cube->occupied_voxels,
    cube->side,
    cube->total_voxels
  );
  if (rc < 0) {
    hv_set_error(err, err_size, "failed to write byte-cube summary: %s", strerror(errno));
    return 0;
  }

  if (cube->max_density == 0u) {
    rc = fprintf(stream, "Max voxel: none\n");
  } else {
    rc = fprintf(
      stream,
      "Max voxel: %u %u %u count=%u\n",
      (unsigned int)cube->max_x,
      (unsigned int)cube->max_y,
      (unsigned int)cube->max_z,
      (unsigned int)cube->max_density
    );
  }
  if (rc < 0) {
    hv_set_error(err, err_size, "failed to write byte-cube detail: %s", strerror(errno));
    return 0;
  }

  return 1;
}
