#include "hilbert3d.h"

#include <stddef.h>

static void hv_hilbert3d_integer_to_transpose(uint32_t order, uint64_t d, uint32_t coords[3])
{
  uint32_t bit = 0u;

  coords[0] = 0u;
  coords[1] = 0u;
  coords[2] = 0u;

  for (bit = 0u; bit < order; ++bit) {
    uint64_t base = (uint64_t)bit * 3u;
    coords[0] |= (uint32_t)(((d >> (base + 2u)) & 1u) << bit);
    coords[1] |= (uint32_t)(((d >> (base + 1u)) & 1u) << bit);
    coords[2] |= (uint32_t)(((d >> (base + 0u)) & 1u) << bit);
  }
}

static void hv_hilbert3d_transpose_to_axes(uint32_t coords[3], uint32_t order)
{
  uint32_t q = 0u;
  uint32_t n = 0u;
  uint32_t t = 0u;
  size_t i = 0u;

  if ((coords == 0) || (order == 0u)) {
    return;
  }

  n = 1u << order;

  t = coords[2] >> 1u;
  for (i = 2u; i > 0u; --i) {
    coords[i] ^= coords[i - 1u];
  }
  coords[0] ^= t;

  for (q = 2u; q != n; q <<= 1u) {
    uint32_t p = q - 1u;

    for (i = 3u; i-- > 0u;) {
      if ((coords[i] & q) != 0u) {
        coords[0] ^= p;
      } else {
        t = (coords[0] ^ coords[i]) & p;
        coords[0] ^= t;
        coords[i] ^= t;
      }
    }
  }
}

int hv_hilbert3d_side_for_order(uint32_t order, uint32_t *side_out)
{
  if (side_out == 0) {
    return 0;
  }
  if ((order < HV_HILBERT3D_MIN_ORDER) || (order > HV_HILBERT3D_MAX_ORDER)) {
    return 0;
  }

  *side_out = 1u << order;
  return 1;
}

int hv_hilbert3d_capacity_for_order(uint32_t order, uint64_t *capacity_out)
{
  if (capacity_out == 0) {
    return 0;
  }
  if ((order < HV_HILBERT3D_MIN_ORDER) || (order > HV_HILBERT3D_MAX_ORDER)) {
    return 0;
  }

  *capacity_out = 1ULL << (order * 3u);
  return 1;
}

int hv_hilbert3d_d2xyz(uint32_t order, uint64_t d, uint32_t *x_out, uint32_t *y_out, uint32_t *z_out)
{
  uint64_t capacity = 0u;
  uint32_t coords[3];

  if ((x_out == 0) || (y_out == 0) || (z_out == 0)) {
    return 0;
  }
  if (!hv_hilbert3d_capacity_for_order(order, &capacity)) {
    return 0;
  }
  if (d >= capacity) {
    return 0;
  }

  hv_hilbert3d_integer_to_transpose(order, d, coords);
  hv_hilbert3d_transpose_to_axes(coords, order);

  *x_out = coords[0];
  *y_out = coords[1];
  *z_out = coords[2];
  return 1;
}
