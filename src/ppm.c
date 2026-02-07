#include "ppm.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void hv_set_error(char *err, size_t err_size, const char *fmt, ...)
{
  va_list args;

  if ((err == 0) || (err_size == 0u)) {
    return;
  }

  va_start(args, fmt);
  (void)vsnprintf(err, err_size, fmt, args);
  va_end(args);
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
  size_t pixel_bytes = 0u;
  size_t written = 0u;
  const char *label = (path_hint != 0) ? path_hint : "(stream)";

  if ((fp == 0) || (pixels == 0) || (width == 0u) || (height == 0u)) {
    hv_set_error(err, err_size, "invalid arguments for ppm write");
    return 0;
  }

  pixel_count = (uint64_t)width * (uint64_t)height;
  if (pixel_count > ((uint64_t)SIZE_MAX / 3u)) {
    hv_set_error(err, err_size, "image too large for host size_t");
    return 0;
  }
  pixel_bytes = (size_t)(pixel_count * 3u);

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
