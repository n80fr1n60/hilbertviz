#include "render.h"

#include "file_io.h"
#include "hilbert.h"
#include "image.h"
#include "palette.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define HV_DEFAULT_PAGE_ORDER 12u
#define HV_READ_CHUNK_BYTES 65536u
#define HV_DEFAULT_MAX_IMAGE_BYTES (256ULL * 1024ULL * 1024ULL)

#if defined(__GNUC__) || defined(__clang__)
#define HV_PRINTF_LIKE(fmt_idx, first_arg_idx) __attribute__((format(printf, fmt_idx, first_arg_idx)))
#else
#define HV_PRINTF_LIKE(fmt_idx, first_arg_idx)
#endif

typedef struct HvByteStats {
  uint64_t total_bytes;
  uint64_t null_bytes;
  uint64_t low_bytes;
  uint64_t ascii_bytes;
  uint64_t high_bytes;
} HvByteStats;

static void HV_PRINTF_LIKE(3, 4) hv_set_error(char *err, size_t err_size, const char *fmt, ...)
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

static int hv_mul_u64(uint64_t a, uint64_t b, uint64_t *out)
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

static int hv_add_u64(uint64_t a, uint64_t b, uint64_t *out)
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

static int hv_add_size(size_t a, size_t b, size_t *out)
{
  if (out == 0) {
    return 0;
  }
  if (b > (SIZE_MAX - a)) {
    return 0;
  }
  *out = a + b;
  return 1;
}

static size_t hv_u64_digits(uint64_t value)
{
  size_t digits = 1u;
  while (value >= 10u) {
    value /= 10u;
    ++digits;
  }
  return digits;
}

static char *hv_strdup_local(const char *text)
{
  size_t len = 0u;
  char *copy = 0;

  if (text == 0) {
    return 0;
  }

  len = strlen(text);
  if (len >= SIZE_MAX) {
    return 0;
  }

  copy = (char *)malloc(len + 1u);
  if (copy == 0) {
    return 0;
  }
  memcpy(copy, text, len + 1u);
  return copy;
}

static char *hv_build_page_output_path(
  const char *base_path,
  uint64_t page_index,
  uint64_t page_count,
  char *err,
  size_t err_size
)
{
  const char *slash = 0;
  const char *dot = 0;
  size_t prefix_len = 0u;
  size_t ext_len = 0u;
  size_t width = 0u;
  char tag[64];
  int n = 0;
  size_t tag_len = 0u;
  size_t total_len = 0u;
  char *out = 0;

  if (base_path == 0) {
    hv_set_error(err, err_size, "missing output path");
    return 0;
  }

  if (page_count <= 1u) {
    out = hv_strdup_local(base_path);
    if (out == 0) {
      hv_set_error(err, err_size, "failed to duplicate output path");
    }
    return out;
  }

  slash = strrchr(base_path, '/');
  dot = strrchr(base_path, '.');
  if ((dot != 0) && (slash != 0) && (dot < slash)) {
    dot = 0;
  }

  if (dot != 0) {
    prefix_len = (size_t)(dot - base_path);
    ext_len = strlen(dot);
  } else {
    prefix_len = strlen(base_path);
    ext_len = 0u;
  }

  width = hv_u64_digits(page_count);
  if (width < 4u) {
    width = 4u;
  }
  if (width > 32u) {
    width = 32u;
  }

  n = snprintf(tag, sizeof(tag), "_page%0*" PRIu64, (int)width, page_index + 1u);
  if ((n <= 0) || ((size_t)n >= sizeof(tag))) {
    hv_set_error(err, err_size, "failed to format page suffix");
    return 0;
  }
  tag_len = (size_t)n;

  if (!hv_add_size(prefix_len, tag_len, &total_len) || !hv_add_size(total_len, ext_len, &total_len) || !hv_add_size(total_len, 1u, &total_len)) {
    hv_set_error(err, err_size, "output path length overflow");
    return 0;
  }

  out = (char *)malloc(total_len);
  if (out == 0) {
    hv_set_error(err, err_size, "failed to allocate output path");
    return 0;
  }

  memcpy(out, base_path, prefix_len);
  memcpy(out + prefix_len, tag, tag_len);
  if (ext_len > 0u) {
    memcpy(out + prefix_len + tag_len, dot, ext_len);
  }
  out[total_len - 1u] = '\0';
  return out;
}

static int hv_same_inode(const struct stat *a, const struct stat *b)
{
  if ((a == 0) || (b == 0)) {
    return 0;
  }
  return (a->st_dev == b->st_dev) && (a->st_ino == b->st_ino);
}

static int hv_normalize_path_local(const char *path, char normalized[PATH_MAX])
{
  char dir_part[PATH_MAX];
  char dir_real[PATH_MAX];
  const char *slash = 0;
  const char *base = 0;
  size_t dir_len = 0u;
  int n = 0;

  if ((path == 0) || (normalized == 0) || (*path == '\0')) {
    return 0;
  }

  slash = strrchr(path, '/');
  if (slash == 0) {
    (void)snprintf(dir_part, sizeof(dir_part), ".");
    base = path;
  } else if (slash == path) {
    (void)snprintf(dir_part, sizeof(dir_part), "/");
    base = slash + 1;
  } else {
    dir_len = (size_t)(slash - path);
    if (dir_len >= sizeof(dir_part)) {
      return 0;
    }
    memcpy(dir_part, path, dir_len);
    dir_part[dir_len] = '\0';
    base = slash + 1;
  }

  if ((base == 0) || (*base == '\0')) {
    return 0;
  }

  if (realpath(dir_part, dir_real) == 0) {
    return 0;
  }

  n = snprintf(normalized, PATH_MAX, "%s/%s", dir_real, base);
  if ((n <= 0) || (n >= PATH_MAX)) {
    return 0;
  }
  return 1;
}

static int hv_paths_alias(const char *a, const char *b)
{
  struct stat st_a;
  struct stat st_b;
  char norm_a[PATH_MAX];
  char norm_b[PATH_MAX];

  if ((a == 0) || (b == 0)) {
    return 0;
  }

  if (strcmp(a, b) == 0) {
    return 1;
  }

  if ((stat(a, &st_a) == 0) && (stat(b, &st_b) == 0) && hv_same_inode(&st_a, &st_b)) {
    return 1;
  }

  if (
    hv_normalize_path_local(a, norm_a) &&
    hv_normalize_path_local(b, norm_b) &&
    (strcmp(norm_a, norm_b) == 0)
  ) {
    return 1;
  }

  return 0;
}

static int hv_path_aliases_input(const char *path, const struct stat *input_st)
{
  struct stat path_st;

  if ((path == 0) || (input_st == 0)) {
    return 0;
  }

  if (stat(path, &path_st) != 0) {
    return 0;
  }

  return hv_same_inode(&path_st, input_st);
}

static int hv_open_checked_output_stream(
  const char *path,
  const char *role,
  const char *input_path,
  const struct stat *input_st,
  FILE **fp_out,
  char *err,
  size_t err_size
)
{
  int fd = -1;
  struct stat out_st;
  FILE *fp = 0;
  const char *role_text = (role != 0) ? role : "output";
  const char *input_text = (input_path != 0) ? input_path : "(input)";

  if ((path == 0) || (input_st == 0) || (fp_out == 0)) {
    hv_set_error(err, err_size, "invalid output-open arguments");
    return 0;
  }
  *fp_out = 0;

  fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0666);
  if (fd < 0) {
    hv_set_error(err, err_size, "failed to open %s '%s': %s", role_text, path, strerror(errno));
    return 0;
  }

  if (fstat(fd, &out_st) != 0) {
    hv_set_error(err, err_size, "failed to stat opened %s '%s': %s", role_text, path, strerror(errno));
    (void)close(fd);
    return 0;
  }

  if (hv_same_inode(&out_st, input_st)) {
    hv_set_error(
      err,
      err_size,
      "refusing destructive path alias: %s path '%s' aliases input '%s'",
      role_text,
      path,
      input_text
    );
    (void)close(fd);
    return 0;
  }

  if (ftruncate(fd, 0) != 0) {
    hv_set_error(err, err_size, "failed to truncate %s '%s': %s", role_text, path, strerror(errno));
    (void)close(fd);
    return 0;
  }

  fp = fdopen(fd, "wb");
  if (fp == 0) {
    hv_set_error(err, err_size, "failed to create stream for %s '%s': %s", role_text, path, strerror(errno));
    (void)close(fd);
    return 0;
  }

  *fp_out = fp;
  return 1;
}

static int hv_preflight_alias_checks(
  const HvRenderOptions *options,
  const HvInputStream *stream,
  uint64_t page_count,
  char *err,
  size_t err_size
)
{
  struct stat input_st;
  int input_fd = -1;
  uint64_t page_index = 0u;

  if ((options == 0) || (stream == 0) || (stream->fp == 0)) {
    hv_set_error(err, err_size, "invalid alias-check arguments");
    return 0;
  }

  input_fd = fileno(stream->fp);
  if ((input_fd < 0) || (fstat(input_fd, &input_st) != 0)) {
    hv_set_error(err, err_size, "failed to stat opened input stream: %s", strerror(errno));
    return 0;
  }

  if (
    options->legend_enabled &&
    (options->legend_path != 0) &&
    hv_path_aliases_input(options->legend_path, &input_st)
  ) {
    hv_set_error(
      err,
      err_size,
      "refusing destructive path alias: legend path '%s' aliases input '%s'",
      options->legend_path,
      options->input_path
    );
    return 0;
  }

  for (page_index = 0u; page_index < page_count; ++page_index) {
    char *page_output_path = hv_build_page_output_path(
      options->output_path,
      page_index,
      page_count,
      err,
      err_size
    );

    if (page_output_path == 0) {
      return 0;
    }

    if (hv_path_aliases_input(page_output_path, &input_st)) {
      hv_set_error(
        err,
        err_size,
        "refusing destructive path alias: output page %" PRIu64 " path '%s' aliases input '%s'",
        page_index + 1u,
        page_output_path,
        options->input_path
      );
      free(page_output_path);
      return 0;
    }

    if (
      options->legend_enabled &&
      (options->legend_path != 0) &&
      hv_paths_alias(page_output_path, options->legend_path)
    ) {
      hv_set_error(
        err,
        err_size,
        "refusing destructive path alias: output page path '%s' aliases legend path '%s'",
        page_output_path,
        options->legend_path
      );
      free(page_output_path);
      return 0;
    }

    free(page_output_path);
  }

  return 1;
}

static int hv_parse_u64_decimal_strict(const char *text, uint64_t *out)
{
  const unsigned char *p = 0;
  unsigned long long parsed = 0;
  char *end = 0;

  if ((text == 0) || (out == 0) || (*text == '\0')) {
    return 0;
  }
  if ((text[0] == '+') || (text[0] == '-')) {
    return 0;
  }

  p = (const unsigned char *)text;
  while (*p != '\0') {
    if ((*p < (unsigned char)'0') || (*p > (unsigned char)'9')) {
      return 0;
    }
    ++p;
  }

  errno = 0;
  parsed = strtoull(text, &end, 10);
  if ((errno != 0) || (end == text) || (*end != '\0')) {
    return 0;
  }

  *out = (uint64_t)parsed;
  return 1;
}

static int hv_resolve_max_image_bytes(uint64_t *out, char *err, size_t err_size)
{
  const char *env = getenv("HILBERTVIZ_MAX_IMAGE_BYTES");
  uint64_t parsed = 0u;

  if (out == 0) {
    hv_set_error(err, err_size, "invalid image-cap arguments");
    return 0;
  }

  if ((env == 0) || (*env == '\0')) {
    *out = HV_DEFAULT_MAX_IMAGE_BYTES;
    return 1;
  }

  if (!hv_parse_u64_decimal_strict(env, &parsed)) {
    hv_set_error(
      err,
      err_size,
      "invalid HILBERTVIZ_MAX_IMAGE_BYTES='%s' (expected unsigned decimal bytes)",
      env
    );
    return 0;
  }

  *out = parsed;
  return 1;
}

static void hv_stats_add_byte(HvByteStats *stats, uint8_t value)
{
  if (stats == 0) {
    return;
  }

  ++stats->total_bytes;
  if (value == 0x00u) {
    ++stats->null_bytes;
  } else if (value <= 0x1Fu) {
    ++stats->low_bytes;
  } else if (value <= 0x7Eu) {
    ++stats->ascii_bytes;
  } else {
    ++stats->high_bytes;
  }
}

static void hv_stats_add(HvByteStats *dst, const HvByteStats *src)
{
  if ((dst == 0) || (src == 0)) {
    return;
  }
  dst->total_bytes += src->total_bytes;
  dst->null_bytes += src->null_bytes;
  dst->low_bytes += src->low_bytes;
  dst->ascii_bytes += src->ascii_bytes;
  dst->high_bytes += src->high_bytes;
}

static const char *hv_layout_name(int layout)
{
  if (layout == HV_LAYOUT_RECT_HILBERT) {
    return "rect-hilbert";
  }
  return "hilbert";
}

static int hv_rect_has_unavoidable_diagonal(uint32_t width, uint32_t height)
{
  uint32_t larger = width;
  uint32_t smaller = height;

  if (height > width) {
    larger = height;
    smaller = width;
  }

  return ((larger & 1u) != 0u) && ((smaller & 1u) == 0u);
}

static int hv_select_geometry(
  const HvRenderOptions *options,
  uint64_t input_bytes,
  uint32_t *order_out,
  uint32_t *width_out,
  uint32_t *height_out,
  uint64_t *capacity_out,
  char *err,
  size_t err_size
)
{
  uint32_t order = 0u;
  uint32_t side = 0u;
  uint32_t width = 0u;
  uint32_t height = 0u;
  uint64_t capacity = 0u;

  if ((options == 0) || (order_out == 0) || (width_out == 0) || (height_out == 0) || (capacity_out == 0)) {
    hv_set_error(err, err_size, "invalid geometry selection arguments");
    return 0;
  }

  if (options->layout == HV_LAYOUT_RECT_HILBERT) {
    if (!options->dimensions_set) {
      hv_set_error(err, err_size, "rect-hilbert layout requires explicit dimensions");
      return 0;
    }
    if ((options->width == 0u) || (options->height == 0u)) {
      hv_set_error(err, err_size, "dimensions must be positive for rect-hilbert layout");
      return 0;
    }
    if (!hv_mul_u64((uint64_t)options->width, (uint64_t)options->height, &capacity) || (capacity == 0u)) {
      hv_set_error(err, err_size, "dimension capacity overflow for %ux%u", options->width, options->height);
      return 0;
    }
    if (options->strict_adjacency && hv_rect_has_unavoidable_diagonal(options->width, options->height)) {
      hv_set_error(
        err,
        err_size,
        "strict adjacency rejects dimensions %ux%u (odd larger side with even smaller side requires a diagonal step)",
        options->width,
        options->height
      );
      return 0;
    }

    order = 0u;
    width = options->width;
    height = options->height;
  } else if (options->layout == HV_LAYOUT_HILBERT) {
    if (options->dimensions_set) {
      hv_set_error(err, err_size, "dimensions are only supported with --layout rect-hilbert");
      return 0;
    }

    if (options->auto_order) {
      if (options->paginate && (input_bytes > 0u)) {
        uint32_t page_order = HV_DEFAULT_PAGE_ORDER;
        uint64_t page_capacity = 0u;
        uint32_t page_side = 0u;

        if (page_order < HV_HILBERT_MIN_ORDER) {
          page_order = HV_HILBERT_MIN_ORDER;
        }
        if (page_order > HV_HILBERT_MAX_ORDER) {
          page_order = HV_HILBERT_MAX_ORDER;
        }

        if (!hv_hilbert_capacity_for_order(page_order, &page_capacity) || !hv_hilbert_side_for_order(page_order, &page_side)) {
          hv_set_error(err, err_size, "failed to compute default page order");
          return 0;
        }

        if (input_bytes <= page_capacity) {
          if (!hv_hilbert_pick_order(input_bytes, &order, &side, &capacity)) {
            hv_set_error(err, err_size, "failed to select auto order");
            return 0;
          }
        } else {
          order = page_order;
          side = page_side;
          capacity = page_capacity;
        }
      } else {
        if (!hv_hilbert_pick_order(input_bytes, &order, &side, &capacity)) {
          hv_set_error(
            err,
            err_size,
            "input slice (%" PRIu64 " bytes) exceeds max order %u capacity; use --paginate",
            input_bytes,
            HV_HILBERT_MAX_ORDER
          );
          return 0;
        }
      }
    } else {
      order = options->order;
      if (!hv_hilbert_side_for_order(order, &side) || !hv_hilbert_capacity_for_order(order, &capacity)) {
        hv_set_error(
          err,
          err_size,
          "invalid order %u (allowed %u..%u)",
          order,
          HV_HILBERT_MIN_ORDER,
          HV_HILBERT_MAX_ORDER
        );
        return 0;
      }
    }

    width = side;
    height = side;
  } else {
    hv_set_error(err, err_size, "unknown layout mode");
    return 0;
  }

  *order_out = order;
  *width_out = width;
  *height_out = height;
  *capacity_out = capacity;
  return 1;
}

static int hv_paint_byte(
  uint8_t *pixels,
  uint64_t pixel_bytes_u64,
  int layout,
  uint32_t order,
  uint32_t width,
  uint32_t height,
  uint64_t d,
  uint8_t value,
  char *err,
  size_t err_size
)
{
  uint32_t x = 0u;
  uint32_t y = 0u;
  uint8_t rgb[3];
  uint64_t row_base = 0u;
  uint64_t row_index = 0u;
  uint64_t byte_index_u64 = 0u;
  size_t byte_index = 0u;

  if (pixels == 0) {
    hv_set_error(err, err_size, "invalid image buffer");
    return 0;
  }
  if ((width == 0u) || (height == 0u)) {
    hv_set_error(err, err_size, "invalid image dimensions");
    return 0;
  }

  hv_byte_to_rgb(value, rgb);
  if (layout == HV_LAYOUT_HILBERT) {
    if (!hv_hilbert_d2xy(order, d, &x, &y)) {
      hv_set_error(err, err_size, "hilbert mapping failed at index %" PRIu64, d);
      return 0;
    }
  } else if (layout == HV_LAYOUT_RECT_HILBERT) {
    if (!hv_gilbert_d2xy(width, height, d, &x, &y)) {
      hv_set_error(err, err_size, "gilbert mapping failed at index %" PRIu64, d);
      return 0;
    }
  } else {
    hv_set_error(err, err_size, "unknown layout mode");
    return 0;
  }

  if ((x >= width) || (y >= height)) {
    hv_set_error(err, err_size, "mapped coordinate out of range");
    return 0;
  }

  if (!hv_mul_u64((uint64_t)y, (uint64_t)width, &row_base) || !hv_add_u64(row_base, (uint64_t)x, &row_index)) {
    hv_set_error(err, err_size, "pixel coordinate overflow");
    return 0;
  }
  if (!hv_mul_u64(row_index, 3u, &byte_index_u64)) {
    hv_set_error(err, err_size, "pixel byte index overflow");
    return 0;
  }
  if ((byte_index_u64 + 2u) >= pixel_bytes_u64) {
    hv_set_error(err, err_size, "pixel index out of range");
    return 0;
  }

  byte_index = (size_t)byte_index_u64;
  pixels[byte_index + 0u] = rgb[0];
  pixels[byte_index + 1u] = rgb[1];
  pixels[byte_index + 2u] = rgb[2];
  return 1;
}

static int hv_write_legend_header(
  FILE *legend_fp,
  const HvRenderOptions *options,
  uint64_t input_bytes,
  uint32_t order,
  uint32_t width,
  uint32_t height,
  uint64_t capacity,
  uint64_t page_count
)
{
  char order_text[32];

  if ((legend_fp == 0) || (options == 0)) {
    return 0;
  }

  if (options->layout == HV_LAYOUT_HILBERT) {
    if (snprintf(order_text, sizeof(order_text), "%u", order) <= 0) {
      return 0;
    }
  } else {
    if (snprintf(order_text, sizeof(order_text), "%s", "n/a") <= 0) {
      return 0;
    }
  }

  if (
    fprintf(
      legend_fp,
      "# hilbertviz legend\n"
      "input=%s\n"
      "output_base=%s\n"
      "layout=%s\n"
      "offset=%" PRIu64 "\n"
      "length=%s\n"
      "order=%s\n"
      "width=%u\n"
      "height=%u\n"
      "capacity_per_page=%" PRIu64 "\n"
      "page_count=%" PRIu64 "\n"
      "input_bytes=%" PRIu64 "\n"
      "columns=page_index,page_bytes,null_bytes,low_bytes,ascii_bytes,high_bytes\n",
      options->input_path,
      options->output_path,
      hv_layout_name(options->layout),
      options->offset,
      options->has_length ? "explicit" : "until_eof",
      order_text,
      width,
      height,
      capacity,
      page_count,
      input_bytes
    ) < 0
  ) {
    return 0;
  }

  return 1;
}

static int hv_write_legend_page(FILE *legend_fp, uint64_t page_index, const HvByteStats *page_stats)
{
  if ((legend_fp == 0) || (page_stats == 0)) {
    return 0;
  }
  if (
    fprintf(
      legend_fp,
      "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
      page_index + 1u,
      page_stats->total_bytes,
      page_stats->null_bytes,
      page_stats->low_bytes,
      page_stats->ascii_bytes,
      page_stats->high_bytes
    ) < 0
  ) {
    return 0;
  }
  return 1;
}

static int hv_write_legend_total(FILE *legend_fp, const HvByteStats *total_stats)
{
  if ((legend_fp == 0) || (total_stats == 0)) {
    return 0;
  }
  if (
    fprintf(
      legend_fp,
      "total,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
      total_stats->total_bytes,
      total_stats->null_bytes,
      total_stats->low_bytes,
      total_stats->ascii_bytes,
      total_stats->high_bytes
    ) < 0
  ) {
    return 0;
  }
  return 1;
}

int hv_render_file(
  const HvRenderOptions *options,
  HvRenderResult *result,
  char *err,
  size_t err_size
)
{
  HvInputStream stream;
  struct stat input_st;
  int input_fd = -1;
  FILE *legend_fp = 0;
  uint32_t order = 0u;
  uint32_t width = 0u;
  uint32_t height = 0u;
  uint64_t capacity = 0u;
  uint64_t pixel_count = 0u;
  uint64_t pixel_bytes_u64 = 0;
  uint64_t max_image_bytes = 0u;
  size_t pixel_bytes = 0;
  uint8_t *pixels = 0;
  uint64_t page_count = 0u;
  uint64_t page_index = 0u;
  uint8_t read_buf[HV_READ_CHUNK_BYTES];
  HvByteStats total_stats;

  memset(&stream, 0, sizeof(stream));
  memset(&total_stats, 0, sizeof(total_stats));

  if (
    (options == 0) || (options->input_path == 0) || (options->output_path == 0) || (result == 0)
  ) {
    hv_set_error(err, err_size, "invalid arguments for render");
    return 0;
  }

  result->order = 0u;
  result->side = 0u;
  result->capacity = 0u;
  result->input_bytes = 0u;
  result->page_count = 0u;

  if (
    !hv_open_file_slice_stream(
      options->input_path,
      options->offset,
      options->has_length,
      options->length,
      &stream,
      err,
      err_size
    )
  ) {
    return 0;
  }

  input_fd = fileno(stream.fp);
  if ((input_fd < 0) || (fstat(input_fd, &input_st) != 0)) {
    hv_set_error(err, err_size, "failed to stat opened input stream: %s", strerror(errno));
    (void)hv_close_input_stream(&stream, 0, 0u);
    return 0;
  }

  if (!hv_select_geometry(options, stream.total, &order, &width, &height, &capacity, err, err_size)) {
    (void)hv_close_input_stream(&stream, 0, 0u);
    return 0;
  }

  if ((stream.total > capacity) && !options->paginate) {
    hv_set_error(
      err,
      err_size,
      "input slice (%" PRIu64 " bytes) exceeds selected capacity (%" PRIu64 " bytes); use --paginate",
      stream.total,
      capacity
    );
    (void)hv_close_input_stream(&stream, 0, 0u);
    return 0;
  }

  if (stream.total == 0u) {
    page_count = 1u;
  } else {
    page_count = (stream.total / capacity) + ((stream.total % capacity) != 0u ? 1u : 0u);
  }

  if (!hv_preflight_alias_checks(options, &stream, page_count, err, err_size)) {
    (void)hv_close_input_stream(&stream, 0, 0u);
    return 0;
  }

  pixel_count = capacity;
  if (!hv_mul_u64(pixel_count, 3u, &pixel_bytes_u64) || (pixel_bytes_u64 > (uint64_t)SIZE_MAX)) {
    hv_set_error(err, err_size, "image size overflow");
    (void)hv_close_input_stream(&stream, 0, 0u);
    return 0;
  }

  if (!hv_resolve_max_image_bytes(&max_image_bytes, err, err_size)) {
    (void)hv_close_input_stream(&stream, 0, 0u);
    return 0;
  }
  if ((max_image_bytes > 0u) && (pixel_bytes_u64 > max_image_bytes)) {
    hv_set_error(
      err,
      err_size,
      "image buffer (%" PRIu64 " bytes) exceeds configured cap (%" PRIu64 " bytes); set HILBERTVIZ_MAX_IMAGE_BYTES to raise/disable (0) the cap",
      pixel_bytes_u64,
      max_image_bytes
    );
    (void)hv_close_input_stream(&stream, 0, 0u);
    return 0;
  }

  pixel_bytes = (size_t)pixel_bytes_u64;

  pixels = (uint8_t *)calloc(pixel_bytes, 1u);
  if (pixels == 0) {
    hv_set_error(err, err_size, "failed to allocate image buffer (%zu bytes)", pixel_bytes);
    (void)hv_close_input_stream(&stream, 0, 0u);
    return 0;
  }

  if (options->legend_enabled) {
    if (options->legend_path == 0) {
      hv_set_error(err, err_size, "legend was enabled but no legend path was provided");
      free(pixels);
      (void)hv_close_input_stream(&stream, 0, 0u);
      return 0;
    }
    if (
      !hv_open_checked_output_stream(
        options->legend_path,
        "legend",
        options->input_path,
        &input_st,
        &legend_fp,
        err,
        err_size
      )
    ) {
      free(pixels);
      (void)hv_close_input_stream(&stream, 0, 0u);
      return 0;
    }
    if (!hv_write_legend_header(legend_fp, options, stream.total, order, width, height, capacity, page_count)) {
      hv_set_error(err, err_size, "failed to write legend header");
      (void)fclose(legend_fp);
      free(pixels);
      (void)hv_close_input_stream(&stream, 0, 0u);
      return 0;
    }
  }

  for (page_index = 0u; page_index < page_count; ++page_index) {
    uint64_t d = 0u;
    uint64_t page_input_bytes = 0u;
    char *page_output_path = 0;
    FILE *page_output_fp = 0;
    HvByteStats page_stats;

    memset(&page_stats, 0, sizeof(page_stats));
    memset(pixels, 0, pixel_bytes);

    page_input_bytes = stream.remaining;
    if (page_input_bytes > capacity) {
      page_input_bytes = capacity;
    }

    while (d < page_input_bytes) {
      size_t chunk = HV_READ_CHUNK_BYTES;
      size_t i = 0u;
      uint64_t remaining_this_page = page_input_bytes - d;

      if (remaining_this_page < (uint64_t)chunk) {
        chunk = (size_t)remaining_this_page;
      }

      if (!hv_stream_read_exact(&stream, read_buf, chunk, err, err_size)) {
        if (legend_fp != 0) {
          (void)fclose(legend_fp);
        }
        free(pixels);
        (void)hv_close_input_stream(&stream, 0, 0u);
        return 0;
      }

      for (i = 0u; i < chunk; ++i, ++d) {
        uint8_t value = read_buf[i];
        hv_stats_add_byte(&page_stats, value);
        if (!hv_paint_byte(pixels, pixel_bytes_u64, options->layout, order, width, height, d, value, err, err_size)) {
          if (legend_fp != 0) {
            (void)fclose(legend_fp);
          }
          free(pixels);
          (void)hv_close_input_stream(&stream, 0, 0u);
          return 0;
        }
      }
    }

    hv_stats_add(&total_stats, &page_stats);

    page_output_path = hv_build_page_output_path(
      options->output_path,
      page_index,
      page_count,
      err,
      err_size
    );
    if (page_output_path == 0) {
      if (legend_fp != 0) {
        (void)fclose(legend_fp);
      }
      free(pixels);
      (void)hv_close_input_stream(&stream, 0, 0u);
      return 0;
    }

    if (
      !hv_open_checked_output_stream(
        page_output_path,
        "output page",
        options->input_path,
        &input_st,
        &page_output_fp,
        err,
        err_size
      )
    ) {
      free(page_output_path);
      if (legend_fp != 0) {
        (void)fclose(legend_fp);
      }
      free(pixels);
      (void)hv_close_input_stream(&stream, 0, 0u);
      return 0;
    }
    if (!hv_write_image_stream(page_output_path, page_output_fp, pixels, width, height, err, err_size)) {
      free(page_output_path);
      (void)fclose(page_output_fp);
      if (legend_fp != 0) {
        (void)fclose(legend_fp);
      }
      free(pixels);
      (void)hv_close_input_stream(&stream, 0, 0u);
      return 0;
    }
    if (fclose(page_output_fp) != 0) {
      hv_set_error(err, err_size, "failed to close output page '%s': %s", page_output_path, strerror(errno));
      free(page_output_path);
      if (legend_fp != 0) {
        (void)fclose(legend_fp);
      }
      free(pixels);
      (void)hv_close_input_stream(&stream, 0, 0u);
      return 0;
    }

    if ((legend_fp != 0) && !hv_write_legend_page(legend_fp, page_index, &page_stats)) {
      hv_set_error(err, err_size, "failed while writing legend page stats");
      free(page_output_path);
      (void)fclose(legend_fp);
      free(pixels);
      (void)hv_close_input_stream(&stream, 0, 0u);
      return 0;
    }

    free(page_output_path);
  }

  if (legend_fp != 0) {
    if (!hv_write_legend_total(legend_fp, &total_stats)) {
      hv_set_error(err, err_size, "failed while writing legend totals");
      (void)fclose(legend_fp);
      free(pixels);
      (void)hv_close_input_stream(&stream, 0, 0u);
      return 0;
    }
    if (fclose(legend_fp) != 0) {
      hv_set_error(err, err_size, "failed to close legend output");
      free(pixels);
      (void)hv_close_input_stream(&stream, 0, 0u);
      return 0;
    }
    legend_fp = 0;
  }

  if (!hv_close_input_stream(&stream, err, err_size)) {
    free(pixels);
    return 0;
  }

  result->order = order;
  result->side = (options->layout == HV_LAYOUT_HILBERT) ? width : 0u;
  result->capacity = capacity;
  result->input_bytes = total_stats.total_bytes;
  result->page_count = page_count;

  free(pixels);
  return 1;
}
