#include "hilbert.h"

static void hv_rot(uint32_t n, uint32_t *x, uint32_t *y, uint32_t rx, uint32_t ry)
{
  if (ry == 0u) {
    if (rx == 1u) {
      *x = (n - 1u) - *x;
      *y = (n - 1u) - *y;
    }

    {
      uint32_t t = *x;
      *x = *y;
      *y = t;
    }
  }
}

int hv_hilbert_side_for_order(uint32_t order, uint32_t *side_out)
{
  if ((side_out == 0) || (order < HV_HILBERT_MIN_ORDER) || (order > HV_HILBERT_MAX_ORDER)) {
    return 0;
  }

  *side_out = 1u << order;
  return 1;
}

int hv_hilbert_capacity_for_order(uint32_t order, uint64_t *capacity_out)
{
  if ((capacity_out == 0) || (order < HV_HILBERT_MIN_ORDER) || (order > HV_HILBERT_MAX_ORDER)) {
    return 0;
  }

  *capacity_out = 1ULL << (2u * order);
  return 1;
}

int hv_hilbert_pick_order(uint64_t byte_count, uint32_t *order_out, uint32_t *side_out, uint64_t *capacity_out)
{
  uint32_t order = HV_HILBERT_MIN_ORDER;
  uint64_t capacity = 0;
  uint32_t side = 0;

  if ((order_out == 0) || (side_out == 0) || (capacity_out == 0)) {
    return 0;
  }

  for (order = HV_HILBERT_MIN_ORDER; order <= HV_HILBERT_MAX_ORDER; ++order) {
    if (!hv_hilbert_capacity_for_order(order, &capacity)) {
      return 0;
    }
    if (byte_count <= capacity) {
      if (!hv_hilbert_side_for_order(order, &side)) {
        return 0;
      }
      *order_out = order;
      *side_out = side;
      *capacity_out = capacity;
      return 1;
    }
  }

  return 0;
}

int hv_hilbert_d2xy(uint32_t order, uint64_t d, uint32_t *x_out, uint32_t *y_out)
{
  uint32_t side = 0;
  uint64_t capacity = 0;
  uint32_t x = 0;
  uint32_t y = 0;
  uint64_t t = d;
  uint64_t s = 0;

  if ((x_out == 0) || (y_out == 0)) {
    return 0;
  }

  if (!hv_hilbert_side_for_order(order, &side) || !hv_hilbert_capacity_for_order(order, &capacity)) {
    return 0;
  }

  if (d >= capacity) {
    return 0;
  }

  for (s = 1u; s < (uint64_t)side; s <<= 1u) {
    uint32_t rx = (uint32_t)((t / 2u) & 1u);
    uint32_t ry = (uint32_t)((t ^ (uint64_t)rx) & 1u);

    hv_rot((uint32_t)s, &x, &y, rx, ry);
    x += (uint32_t)(s * (uint64_t)rx);
    y += (uint32_t)(s * (uint64_t)ry);
    t /= 4u;
  }

  *x_out = x;
  *y_out = y;
  return 1;
}
