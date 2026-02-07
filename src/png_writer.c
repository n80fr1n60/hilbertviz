#include "png_writer.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef HV_HAVE_LIBPNG
#include <png.h>
#endif

static void hv_set_error(char *err, size_t err_size, const char *fmt, ...)
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

int hv_write_png(
  const char *path,
  const uint8_t *pixels,
  uint32_t width,
  uint32_t height,
  char *err,
  size_t err_size
)
{
#ifdef HV_HAVE_LIBPNG
  FILE *fp = 0;

  if ((path == 0) || (pixels == 0) || (width == 0u) || (height == 0u)) {
    hv_set_error(err, err_size, "invalid arguments for png write");
    return 0;
  }

  fp = fopen(path, "wb");
  if (fp == 0) {
    hv_set_error(err, err_size, "failed to open output '%s': %s", path, strerror(errno));
    return 0;
  }

  if (!hv_write_png_stream(fp, path, pixels, width, height, err, err_size)) {
    (void)fclose(fp);
    return 0;
  }

  if (fclose(fp) != 0) {
    hv_set_error(err, err_size, "failed to close output '%s': %s", path, strerror(errno));
    return 0;
  }
  return 1;
#else
  (void)pixels;
  (void)width;
  (void)height;
  hv_set_error(
    err,
    err_size,
    "png output requested for '%s' but libpng is not available in this build",
    (path != 0) ? path : "(null)"
  );
  return 0;
#endif
}

int hv_write_png_stream(
  FILE *fp,
  const char *path_hint,
  const uint8_t *pixels,
  uint32_t width,
  uint32_t height,
  char *err,
  size_t err_size
)
{
#ifdef HV_HAVE_LIBPNG
  png_structp png_ptr = 0;
  png_infop info_ptr = 0;
  uint64_t row_bytes_u64 = 0u;
  size_t row_bytes = 0u;
  uint32_t y = 0u;
  (void)path_hint;

  if ((fp == 0) || (pixels == 0) || (width == 0u) || (height == 0u)) {
    hv_set_error(err, err_size, "invalid arguments for png write");
    return 0;
  }

  row_bytes_u64 = (uint64_t)width * 3u;
  if (row_bytes_u64 > (uint64_t)SIZE_MAX) {
    hv_set_error(err, err_size, "png row size overflow");
    return 0;
  }
  if ((height > 0u) && (row_bytes_u64 > ((uint64_t)SIZE_MAX / (uint64_t)height))) {
    hv_set_error(err, err_size, "png image too large for host size_t");
    return 0;
  }
  row_bytes = (size_t)row_bytes_u64;

  png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0);
  if (png_ptr == 0) {
    hv_set_error(err, err_size, "failed to initialize libpng write struct");
    return 0;
  }

  info_ptr = png_create_info_struct(png_ptr);
  if (info_ptr == 0) {
    hv_set_error(err, err_size, "failed to initialize libpng info struct");
    png_destroy_write_struct(&png_ptr, 0);
    return 0;
  }

  if (setjmp(png_jmpbuf(png_ptr)) != 0) {
    hv_set_error(err, err_size, "libpng failed while writing png stream");
    png_destroy_write_struct(&png_ptr, &info_ptr);
    return 0;
  }

  png_init_io(png_ptr, fp);
  png_set_IHDR(
    png_ptr,
    info_ptr,
    width,
    height,
    8,
    PNG_COLOR_TYPE_RGB,
    PNG_INTERLACE_NONE,
    PNG_COMPRESSION_TYPE_BASE,
    PNG_FILTER_TYPE_BASE
  );

  png_write_info(png_ptr, info_ptr);
  for (y = 0u; y < height; ++y) {
    const png_bytep row = (const png_bytep)(pixels + ((size_t)y * row_bytes));
    png_write_row(png_ptr, row);
  }
  png_write_end(png_ptr, info_ptr);

  png_destroy_write_struct(&png_ptr, &info_ptr);
  return 1;
#else
  (void)fp;
  (void)path_hint;
  (void)pixels;
  (void)width;
  (void)height;
  hv_set_error(
    err,
    err_size,
    "png output requested for '%s' but libpng is not available in this build",
    (path_hint != 0) ? path_hint : "(null)"
  );
  return 0;
#endif
}
