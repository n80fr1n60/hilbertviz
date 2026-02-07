#ifndef IMAGE_H
#define IMAGE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

int hv_write_image(
  const char *path,
  const uint8_t *pixels,
  uint32_t width,
  uint32_t height,
  char *err,
  size_t err_size
);

int hv_write_image_stream(
  const char *path,
  FILE *fp,
  const uint8_t *pixels,
  uint32_t width,
  uint32_t height,
  char *err,
  size_t err_size
);

#endif
