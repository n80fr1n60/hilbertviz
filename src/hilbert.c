#include "hilbert.h"

#define HV_GILBERT_DEFAULT_MAX_RECURSION_DEPTH 256u

static int hv_i64_add(int64_t a, int64_t b, int64_t *out)
{
  if (out == 0) {
    return 0;
  }

  if (((b > 0) && (a > (INT64_MAX - b))) || ((b < 0) && (a < (INT64_MIN - b)))) {
    return 0;
  }

  *out = a + b;
  return 1;
}

static int hv_i64_sub(int64_t a, int64_t b, int64_t *out)
{
#if defined(__clang__) || defined(__GNUC__)
  if (out == 0) {
    return 0;
  }
  return __builtin_sub_overflow(a, b, out) == 0;
#else
  if (out == 0) {
    return 0;
  }
  if ((b > 0) && (a < (INT64_MIN + b))) {
    return 0;
  }
  if ((b < 0) && (a > (INT64_MAX + b))) {
    return 0;
  }
  *out = a - b;
  return 1;
#endif
}

static int hv_i64_neg(int64_t value, int64_t *out)
{
  if (out == 0) {
    return 0;
  }
  if (value == INT64_MIN) {
    return 0;
  }
  *out = -value;
  return 1;
}

static int hv_i64_mul(int64_t a, int64_t b, int64_t *out)
{
#if defined(__clang__) || defined(__GNUC__)
  if (out == 0) {
    return 0;
  }
  return __builtin_mul_overflow(a, b, out) == 0;
#else
  if (out == 0) {
    return 0;
  }
  if ((a == 0) || (b == 0)) {
    *out = 0;
    return 1;
  }
  if ((a == -1) && (b == INT64_MIN)) {
    return 0;
  }
  if ((b == -1) && (a == INT64_MIN)) {
    return 0;
  }
  if (a > (INT64_MAX / b) || a < (INT64_MIN / b)) {
    return 0;
  }
  *out = a * b;
  return 1;
#endif
}

static int hv_u64_to_i64(uint64_t value, int64_t *out)
{
  if (out == 0) {
    return 0;
  }
  if (value > (uint64_t)INT64_MAX) {
    return 0;
  }
  *out = (int64_t)value;
  return 1;
}

static int hv_i64_abs_to_u64(int64_t value, uint64_t *out)
{
  if (out == 0) {
    return 0;
  }

  if (value >= 0) {
    *out = (uint64_t)value;
    return 1;
  }

  if (value == INT64_MIN) {
    return 0;
  }

  *out = (uint64_t)(-value);
  return 1;
}

static int hv_u64_add(uint64_t a, uint64_t b, uint64_t *out)
{
  if (out == 0) {
    return 0;
  }
  if (b > (UINT64_MAX - a)) {
    return 0;
  }
  *out = a + b;
  return 1;
}

static int hv_u64_mul(uint64_t a, uint64_t b, uint64_t *out)
{
  if (out == 0) {
    return 0;
  }
  if ((a != 0u) && (b > (UINT64_MAX / a))) {
    return 0;
  }
  *out = a * b;
  return 1;
}

static int64_t hv_floor_div2_i64(int64_t value)
{
  if (value >= 0) {
    return value / 2;
  }

  return -(((-value) + 1) / 2);
}

static int64_t hv_sign_i64(int64_t value)
{
  if (value > 0) {
    return 1;
  }
  if (value < 0) {
    return -1;
  }
  return 0;
}

static int hv_gilbert_dims(
  int64_t ax,
  int64_t ay,
  int64_t bx,
  int64_t by,
  uint64_t *w_out,
  uint64_t *h_out
)
{
  int64_t w_signed = 0;
  int64_t h_signed = 0;

  if ((w_out == 0) || (h_out == 0)) {
    return 0;
  }

  if (!hv_i64_add(ax, ay, &w_signed) || !hv_i64_add(bx, by, &h_signed)) {
    return 0;
  }

  if (!hv_i64_abs_to_u64(w_signed, w_out) || !hv_i64_abs_to_u64(h_signed, h_out)) {
    return 0;
  }

  return (*w_out > 0u) && (*h_out > 0u);
}

static int hv_gilbert_cell_count(int64_t ax, int64_t ay, int64_t bx, int64_t by, uint64_t *count_out)
{
  uint64_t w = 0u;
  uint64_t h = 0u;

  if (!hv_gilbert_dims(ax, ay, bx, by, &w, &h)) {
    return 0;
  }
  return hv_u64_mul(w, h, count_out);
}

static int hv_gilbert_d2xy_recursive(
  int64_t x,
  int64_t y,
  int64_t ax,
  int64_t ay,
  int64_t bx,
  int64_t by,
  uint64_t d,
  uint32_t depth,
  uint32_t max_depth,
  int64_t *x_out,
  int64_t *y_out
)
{
  uint64_t w = 0u;
  uint64_t h = 0u;
  uint64_t total = 0u;
  int64_t dax = 0;
  int64_t day = 0;
  int64_t dbx = 0;
  int64_t dby = 0;
  int64_t ax2 = 0;
  int64_t ay2 = 0;
  int64_t bx2 = 0;
  int64_t by2 = 0;
  uint64_t w2 = 0u;
  uint64_t h2 = 0u;

  if ((x_out == 0) || (y_out == 0)) {
    return 0;
  }
  if (depth > max_depth) {
    return 0;
  }

  if (!hv_gilbert_dims(ax, ay, bx, by, &w, &h) || !hv_u64_mul(w, h, &total)) {
    return 0;
  }
  if (d >= total) {
    return 0;
  }

  dax = hv_sign_i64(ax);
  day = hv_sign_i64(ay);
  dbx = hv_sign_i64(bx);
  dby = hv_sign_i64(by);

  if (h == 1u) {
    int64_t d_i64 = 0;
    int64_t x_delta = 0;
    int64_t y_delta = 0;

    if (
      !hv_u64_to_i64(d, &d_i64) ||
      !hv_i64_mul(dax, d_i64, &x_delta) ||
      !hv_i64_mul(day, d_i64, &y_delta)
    ) {
      return 0;
    }
    if (!hv_i64_add(x, x_delta, x_out) || !hv_i64_add(y, y_delta, y_out)) {
      return 0;
    }
    return 1;
  }

  if (w == 1u) {
    int64_t d_i64 = 0;
    int64_t x_delta = 0;
    int64_t y_delta = 0;

    if (
      !hv_u64_to_i64(d, &d_i64) ||
      !hv_i64_mul(dbx, d_i64, &x_delta) ||
      !hv_i64_mul(dby, d_i64, &y_delta)
    ) {
      return 0;
    }
    if (!hv_i64_add(x, x_delta, x_out) || !hv_i64_add(y, y_delta, y_out)) {
      return 0;
    }
    return 1;
  }

  ax2 = hv_floor_div2_i64(ax);
  ay2 = hv_floor_div2_i64(ay);
  bx2 = hv_floor_div2_i64(bx);
  by2 = hv_floor_div2_i64(by);

  if (!hv_gilbert_dims(ax2, ay2, bx2, by2, &w2, &h2)) {
    return 0;
  }

  if ((2u * w) > (3u * h)) {
    uint64_t first_count = 0u;
    int64_t x2 = 0;
    int64_t y2 = 0;

    if ((w2 % 2u) != 0u && (w > 2u)) {
      if (!hv_i64_add(ax2, dax, &ax2) || !hv_i64_add(ay2, day, &ay2)) {
        return 0;
      }
    }

    if (!hv_gilbert_cell_count(ax2, ay2, bx, by, &first_count)) {
      return 0;
    }

    if (d < first_count) {
      return hv_gilbert_d2xy_recursive(
        x,
        y,
        ax2,
        ay2,
        bx,
        by,
        d,
        depth + 1u,
        max_depth,
        x_out,
        y_out
      );
    }

    if (!hv_i64_add(x, ax2, &x2) || !hv_i64_add(y, ay2, &y2)) {
      return 0;
    }

    if (!hv_i64_sub(ax, ax2, &ax) || !hv_i64_sub(ay, ay2, &ay)) {
      return 0;
    }

    return hv_gilbert_d2xy_recursive(
      x2,
      y2,
      ax,
      ay,
      bx,
      by,
      d - first_count,
      depth + 1u,
      max_depth,
      x_out,
      y_out
    );
  }

  {
    uint64_t first_count = 0u;
    uint64_t second_count = 0u;
    uint64_t first_two = 0u;
    int64_t bx_rem_for_count = 0;
    int64_t by_rem_for_count = 0;
    int64_t x2 = 0;
    int64_t y2 = 0;
    int64_t x3 = 0;
    int64_t y3 = 0;
    int64_t x3_part1 = 0;
    int64_t y3_part1 = 0;
    int64_t x3_part2 = 0;
    int64_t y3_part2 = 0;

    if ((h2 % 2u) != 0u && (h > 2u)) {
      if (!hv_i64_add(bx2, dbx, &bx2) || !hv_i64_add(by2, dby, &by2)) {
        return 0;
      }
    }

    if (!hv_i64_sub(bx, bx2, &bx_rem_for_count) || !hv_i64_sub(by, by2, &by_rem_for_count)) {
      return 0;
    }

    if (
      !hv_gilbert_cell_count(bx2, by2, ax2, ay2, &first_count) ||
      !hv_gilbert_cell_count(ax, ay, bx_rem_for_count, by_rem_for_count, &second_count) ||
      !hv_u64_add(first_count, second_count, &first_two)
    ) {
      return 0;
    }

    if (d < first_count) {
      return hv_gilbert_d2xy_recursive(
        x,
        y,
        bx2,
        by2,
        ax2,
        ay2,
        d,
        depth + 1u,
        max_depth,
        x_out,
        y_out
      );
    }

    if (d < first_two) {
      int64_t bx_rem = 0;
      int64_t by_rem = 0;
      if (!hv_i64_add(x, bx2, &x2) || !hv_i64_add(y, by2, &y2)) {
        return 0;
      }
      if (!hv_i64_sub(bx, bx2, &bx_rem) || !hv_i64_sub(by, by2, &by_rem)) {
        return 0;
      }
      return hv_gilbert_d2xy_recursive(
        x2,
        y2,
        ax,
        ay,
        bx_rem,
        by_rem,
        d - first_count,
        depth + 1u,
        max_depth,
        x_out,
        y_out
      );
    }

    {
      int64_t neg_dax = 0;
      int64_t neg_day = 0;
      int64_t neg_dbx = 0;
      int64_t neg_dby = 0;
      int64_t bx2_neg = 0;
      int64_t by2_neg = 0;
      int64_t ax_rem = 0;
      int64_t ay_rem = 0;
      int64_t ax_rem_neg = 0;
      int64_t ay_rem_neg = 0;

      if (
        !hv_i64_neg(dax, &neg_dax) ||
        !hv_i64_neg(day, &neg_day) ||
        !hv_i64_neg(dbx, &neg_dbx) ||
        !hv_i64_neg(dby, &neg_dby)
      ) {
        return 0;
      }

      if (
        !hv_i64_add(ax, neg_dax, &x3_part1) ||
        !hv_i64_add(ay, neg_day, &y3_part1) ||
        !hv_i64_add(bx2, neg_dbx, &x3_part2) ||
        !hv_i64_add(by2, neg_dby, &y3_part2) ||
        !hv_i64_add(x, x3_part1, &x3) ||
        !hv_i64_add(x3, x3_part2, &x3) ||
        !hv_i64_add(y, y3_part1, &y3) ||
        !hv_i64_add(y3, y3_part2, &y3)
      ) {
        return 0;
      }

      if (
        !hv_i64_neg(bx2, &bx2_neg) ||
        !hv_i64_neg(by2, &by2_neg) ||
        !hv_i64_sub(ax, ax2, &ax_rem) ||
        !hv_i64_sub(ay, ay2, &ay_rem) ||
        !hv_i64_neg(ax_rem, &ax_rem_neg) ||
        !hv_i64_neg(ay_rem, &ay_rem_neg)
      ) {
        return 0;
      }

      return hv_gilbert_d2xy_recursive(
        x3,
        y3,
        bx2_neg,
        by2_neg,
        ax_rem_neg,
        ay_rem_neg,
        d - first_two,
        depth + 1u,
        max_depth,
        x_out,
        y_out
      );
    }
  }
}

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

int hv_gilbert_d2xy(uint32_t width, uint32_t height, uint64_t d, uint32_t *x_out, uint32_t *y_out)
{
  return hv_gilbert_d2xy_with_limit(
    width,
    height,
    d,
    HV_GILBERT_DEFAULT_MAX_RECURSION_DEPTH,
    x_out,
    y_out
  );
}

int hv_gilbert_d2xy_with_limit(
  uint32_t width,
  uint32_t height,
  uint64_t d,
  uint32_t max_depth,
  uint32_t *x_out,
  uint32_t *y_out
)
{
  int64_t x = 0;
  int64_t y = 0;
  uint64_t capacity = 0u;

  if ((x_out == 0) || (y_out == 0) || (width == 0u) || (height == 0u)) {
    return 0;
  }

  if (!hv_u64_mul((uint64_t)width, (uint64_t)height, &capacity) || (d >= capacity)) {
    return 0;
  }

  if (width >= height) {
    if (
      !hv_gilbert_d2xy_recursive(
        0,
        0,
        (int64_t)width,
        0,
        0,
        (int64_t)height,
        d,
        0u,
        max_depth,
        &x,
        &y
      )
    ) {
      return 0;
    }
  } else {
    if (
      !hv_gilbert_d2xy_recursive(
        0,
        0,
        0,
        (int64_t)height,
        (int64_t)width,
        0,
        d,
        0u,
        max_depth,
        &x,
        &y
      )
    ) {
      return 0;
    }
  }

  if ((x < 0) || (y < 0) || (x >= (int64_t)width) || (y >= (int64_t)height)) {
    return 0;
  }

  *x_out = (uint32_t)x;
  *y_out = (uint32_t)y;
  return 1;
}
