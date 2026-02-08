#include "image.h"

#include "png_writer.h"
#include "ppm.h"

#include <ctype.h>
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

static int hv_ends_with_casefold(const char *path, const char *suffix)
{
  size_t path_len = 0u;
  size_t suffix_len = 0u;
  size_t i = 0u;

  if ((path == 0) || (suffix == 0)) {
    return 0;
  }

  path_len = strlen(path);
  suffix_len = strlen(suffix);
  if (path_len < suffix_len) {
    return 0;
  }

  for (i = 0u; i < suffix_len; ++i) {
    unsigned char a = (unsigned char)path[(path_len - suffix_len) + i];
    unsigned char b = (unsigned char)suffix[i];
    if (tolower(a) != tolower(b)) {
      return 0;
    }
  }
  return 1;
}

int hv_write_image(
  const char *path,
  const uint8_t *pixels,
  uint32_t width,
  uint32_t height,
  char *err,
  size_t err_size
)
{
  if (path == 0) {
    hv_set_error(err, err_size, "missing output path");
    return 0;
  }

  if (hv_ends_with_casefold(path, ".png")) {
    return hv_write_png(path, pixels, width, height, err, err_size);
  }

  if (hv_ends_with_casefold(path, ".ppm") || (strchr(path, '.') == 0)) {
    return hv_write_ppm(path, pixels, width, height, err, err_size);
  }

  hv_set_error(
    err,
    err_size,
    "unsupported output extension for '%s' (use .ppm or .png)",
    path
  );
  return 0;
}

int hv_write_image_stream(
  const char *path,
  FILE *fp,
  const uint8_t *pixels,
  uint32_t width,
  uint32_t height,
  char *err,
  size_t err_size
)
{
  if (path == 0) {
    hv_set_error(err, err_size, "missing output path");
    return 0;
  }
  if (fp == 0) {
    hv_set_error(err, err_size, "missing output stream");
    return 0;
  }

  if (hv_ends_with_casefold(path, ".png")) {
    return hv_write_png_stream(fp, path, pixels, width, height, err, err_size);
  }

  if (hv_ends_with_casefold(path, ".ppm") || (strchr(path, '.') == 0)) {
    return hv_write_ppm_stream(fp, path, pixels, width, height, err, err_size);
  }

  hv_set_error(
    err,
    err_size,
    "unsupported output extension for '%s' (use .ppm or .png)",
    path
  );
  return 0;
}
