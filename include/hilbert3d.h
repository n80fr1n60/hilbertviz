#ifndef HILBERT3D_H
#define HILBERT3D_H

#include <stdint.h>

#define HV_HILBERT3D_MIN_ORDER 1u
#define HV_HILBERT3D_MAX_ORDER 7u

int hv_hilbert3d_side_for_order(uint32_t order, uint32_t *side_out);
int hv_hilbert3d_capacity_for_order(uint32_t order, uint64_t *capacity_out);
int hv_hilbert3d_d2xyz(uint32_t order, uint64_t d, uint32_t *x_out, uint32_t *y_out, uint32_t *z_out);

#endif
