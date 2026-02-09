#ifndef RENDER_H
#define RENDER_H

#include <stddef.h>
#include <stdint.h>

#define HV_LAYOUT_HILBERT 0
#define HV_LAYOUT_RECT_HILBERT 1

typedef struct HvRenderOptions {
  const char *input_path;
  const char *output_path;
  const char *legend_path;
  uint64_t offset;
  int has_length;
  uint64_t length;
  int auto_order;
  uint32_t order;
  int paginate;
  int legend_enabled;
  int layout;
  int dimensions_set;
  uint32_t width;
  uint32_t height;
  int strict_adjacency;
} HvRenderOptions;

typedef struct HvRenderResult {
  uint32_t order;
  uint32_t side;
  uint64_t capacity;
  uint64_t input_bytes;
  uint64_t page_count;
} HvRenderResult;

int hv_render_file(
  const HvRenderOptions *options,
  HvRenderResult *result,
  char *err,
  size_t err_size
);

#endif
