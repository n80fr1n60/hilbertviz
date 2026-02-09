#include "ppm.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

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

static int hv_u64_mul(uint64_t a, uint64_t b, uint64_t *out)
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

int hv_write_ppm(
  const char *path,
  const uint8_t *pixels,
  uint32_t width,
  uint32_t height,
  char *err,
  size_t err_size
)
{
  FILE *fp = 0;

  if ((path == 0) || (pixels == 0) || (width == 0u) || (height == 0u)) {
    hv_set_error(err, err_size, "invalid arguments for ppm write");
    return 0;
  }

  fp = fopen(path, "wb");
  if (fp == 0) {
    hv_set_error(err, err_size, "failed to open output '%s': %s", path, strerror(errno));
    return 0;
  }

  if (!hv_write_ppm_stream(fp, path, pixels, width, height, err, err_size)) {
    (void)fclose(fp);
    return 0;
  }

  if (fclose(fp) != 0) {
    hv_set_error(err, err_size, "failed to close output '%s': %s", path, strerror(errno));
    return 0;
  }

  return 1;
}

int hv_write_ppm_stream(
  FILE *fp,
  const char *path_hint,
  const uint8_t *pixels,
  uint32_t width,
  uint32_t height,
  char *err,
  size_t err_size
)
{
  uint64_t pixel_count = 0u;
  uint64_t pixel_bytes_u64 = 0u;
  size_t pixel_bytes = 0u;
  size_t written = 0u;
  const char *label = (path_hint != 0) ? path_hint : "(stream)";

  if ((fp == 0) || (pixels == 0) || (width == 0u) || (height == 0u)) {
    hv_set_error(err, err_size, "invalid arguments for ppm write");
    return 0;
  }

  if (!hv_u64_mul((uint64_t)width, (uint64_t)height, &pixel_count)) {
    hv_set_error(err, err_size, "ppm pixel count overflow");
    return 0;
  }
  if (!hv_u64_mul(pixel_count, 3u, &pixel_bytes_u64) || (pixel_bytes_u64 > (uint64_t)SIZE_MAX)) {
    hv_set_error(err, err_size, "image too large for host size_t");
    return 0;
  }
  pixel_bytes = (size_t)pixel_bytes_u64;

  if (fprintf(fp, "P6\n%u %u\n255\n", width, height) <= 0) {
    hv_set_error(err, err_size, "failed to write ppm header to '%s'", label);
    return 0;
  }

  written = fwrite(pixels, 1u, pixel_bytes, fp);
  if (written != pixel_bytes) {
    hv_set_error(err, err_size, "failed to write ppm payload to '%s'", label);
    return 0;
  }

  return 1;
}
