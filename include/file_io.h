#ifndef FILE_IO_H
#define FILE_IO_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct HvBuffer {
  uint8_t *data;
  size_t size;
} HvBuffer;

typedef struct HvInputStream {
  FILE *fp;
  uint64_t remaining;
  uint64_t total;
} HvInputStream;

int hv_read_file_slice(
  const char *path,
  uint64_t offset,
  int has_length,
  uint64_t length,
  HvBuffer *out,
  char *err,
  size_t err_size
);

void hv_free_buffer(HvBuffer *buffer);

int hv_open_file_slice_stream(
  const char *path,
  uint64_t offset,
  int has_length,
  uint64_t length,
  HvInputStream *stream,
  char *err,
  size_t err_size
);

int hv_stream_read_exact(
  HvInputStream *stream,
  uint8_t *buf,
  size_t size,
  char *err,
  size_t err_size
);

int hv_close_input_stream(HvInputStream *stream, char *err, size_t err_size);

#endif
