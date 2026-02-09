#ifndef HILBERT_H
#define HILBERT_H

#include <stdint.h>

#define HV_HILBERT_MIN_ORDER 1u
#define HV_HILBERT_MAX_ORDER 16u

int hv_hilbert_side_for_order(uint32_t order, uint32_t *side_out);
int hv_hilbert_capacity_for_order(uint32_t order, uint64_t *capacity_out);
int hv_hilbert_pick_order(uint64_t byte_count, uint32_t *order_out, uint32_t *side_out, uint64_t *capacity_out);
int hv_hilbert_d2xy(uint32_t order, uint64_t d, uint32_t *x_out, uint32_t *y_out);
int hv_gilbert_d2xy(uint32_t width, uint32_t height, uint64_t d, uint32_t *x_out, uint32_t *y_out);
int hv_gilbert_d2xy_with_limit(
  uint32_t width,
  uint32_t height,
  uint64_t d,
  uint32_t max_depth,
  uint32_t *x_out,
  uint32_t *y_out
);

#endif
