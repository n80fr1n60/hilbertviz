#ifndef PPM_H
#define PPM_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

int hv_write_ppm(
  const char *path,
  const uint8_t *pixels,
  uint32_t width,
  uint32_t height,
  char *err,
  size_t err_size
);

int hv_write_ppm_stream(
  FILE *fp,
  const char *path_hint,
  const uint8_t *pixels,
  uint32_t width,
  uint32_t height,
  char *err,
  size_t err_size
);

#endif
