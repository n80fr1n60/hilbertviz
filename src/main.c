#include "file_io.h"
#include "hilbert.h"
#include "render.h"

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  HV_OPT_LAYOUT = 1000,
  HV_OPT_DIMENSIONS,
  HV_OPT_DRY_RUN,
  HV_OPT_STRICT_ADJACENCY
};

static void hv_print_usage(const char *prog)
{
  fprintf(
    stderr,
    "Usage: %s <input.bin> -o <output.{ppm|png}> [options]\n"
    "Options:\n"
    "  -o, --output <path>      Output image path (required, .ppm or .png)\n"
    "  -n, --order <N>          Hilbert order (%u..%u)\n"
    "  -a, --auto-order         Auto-pick smallest order to fit data (default)\n"
    "  -f, --offset <bytes>     Read input starting at offset\n"
    "  -l, --length <bytes>     Read only this many bytes from offset\n"
    "  -p, --paginate           Emit multiple pages when input exceeds one image\n"
    "  -g, --legend             Write sidecar legend stats file (default: <output>.legend.txt)\n"
    "  -G, --legend-path <p>    Explicit legend output path\n"
    "      --layout <name>      Layout: hilbert (default) or rect-hilbert\n"
    "      --dimensions <WxH>   Dimensions for rect-hilbert mode\n"
    "      --strict-adjacency   Reject odd/even parity dimensions that require a diagonal step\n"
    "      --dry-run            Print fit/planning details without writing output files\n"
    "  -h, --help               Show this help\n",
    prog,
    HV_HILBERT_MIN_ORDER,
    HV_HILBERT_MAX_ORDER
  );
}

static int hv_parse_u64(const char *text, uint64_t *out)
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

static int hv_parse_order(const char *text, uint32_t *out)
{
  uint64_t parsed = 0;
  if (!hv_parse_u64(text, &parsed)) {
    return 0;
  }
  if ((parsed < (uint64_t)HV_HILBERT_MIN_ORDER) || (parsed > (uint64_t)HV_HILBERT_MAX_ORDER)) {
    return 0;
  }
  *out = (uint32_t)parsed;
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

static uint64_t hv_ceil_div_u64(uint64_t numer, uint64_t denom)
{
  if (denom == 0u) {
    return 0u;
  }
  return (numer / denom) + ((numer % denom) != 0u ? 1u : 0u);
}

static int hv_parse_layout(const char *text, int *layout_out)
{
  if ((text == 0) || (layout_out == 0)) {
    return 0;
  }

  if (strcmp(text, "hilbert") == 0) {
    *layout_out = HV_LAYOUT_HILBERT;
    return 1;
  }
  if (strcmp(text, "rect-hilbert") == 0) {
    *layout_out = HV_LAYOUT_RECT_HILBERT;
    return 1;
  }

  return 0;
}

static int hv_parse_dimensions(const char *text, uint32_t *width_out, uint32_t *height_out)
{
  const char *sep = 0;
  size_t left_len = 0u;
  size_t right_len = 0u;
  char left[32];
  char right[32];
  uint64_t width_u64 = 0u;
  uint64_t height_u64 = 0u;

  if ((text == 0) || (width_out == 0) || (height_out == 0)) {
    return 0;
  }

  sep = strchr(text, 'x');
  if (sep == 0) {
    sep = strchr(text, 'X');
  }
  if ((sep == 0) || (sep == text) || (sep[1] == '\0')) {
    return 0;
  }
  if ((strchr(sep + 1, 'x') != 0) || (strchr(sep + 1, 'X') != 0)) {
    return 0;
  }

  left_len = (size_t)(sep - text);
  right_len = strlen(sep + 1);
  if ((left_len == 0u) || (right_len == 0u) || (left_len >= sizeof(left)) || (right_len >= sizeof(right))) {
    return 0;
  }

  memcpy(left, text, left_len);
  left[left_len] = '\0';
  memcpy(right, sep + 1, right_len + 1u);

  if (!hv_parse_u64(left, &width_u64) || !hv_parse_u64(right, &height_u64)) {
    return 0;
  }
  if ((width_u64 == 0u) || (height_u64 == 0u) || (width_u64 > (uint64_t)UINT32_MAX) || (height_u64 > (uint64_t)UINT32_MAX)) {
    return 0;
  }

  *width_out = (uint32_t)width_u64;
  *height_out = (uint32_t)height_u64;
  return 1;
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

static int hv_make_default_legend_path(const char *output_path, char **path_out)
{
  static const char suffix[] = ".legend.txt";
  size_t out_len = 0u;
  size_t total_len = 0u;
  char *path = 0;

  if ((output_path == 0) || (path_out == 0)) {
    return 0;
  }

  *path_out = 0;
  out_len = strlen(output_path);
  if (!hv_add_size(out_len, sizeof(suffix), &total_len)) {
    return 0;
  }

  path = (char *)malloc(total_len);
  if (path == 0) {
    return 0;
  }

  memcpy(path, output_path, out_len);
  memcpy(path + out_len, suffix, sizeof(suffix));
  *path_out = path;
  return 1;
}

static int hv_compute_slice_bytes(
  const char *input_path,
  uint64_t offset,
  int has_length,
  uint64_t length,
  uint64_t *slice_bytes_out,
  char *err,
  size_t err_size
)
{
  HvInputStream stream;

  if ((input_path == 0) || (slice_bytes_out == 0)) {
    return 0;
  }

  memset(&stream, 0, sizeof(stream));
  if (!hv_open_file_slice_stream(input_path, offset, has_length, length, &stream, err, err_size)) {
    return 0;
  }

  *slice_bytes_out = stream.total;
  if (!hv_close_input_stream(&stream, err, err_size)) {
    return 0;
  }

  return 1;
}

static void hv_print_rect_suggestions(uint64_t slice_bytes, uint32_t width, uint32_t height)
{
  uint64_t needed_w = 0u;
  uint64_t needed_h = 0u;
  uint64_t even_w = (uint64_t)width;
  uint64_t even_h = (uint64_t)height;

  if ((even_w & 1u) != 0u) {
    ++even_w;
  }
  if ((even_h & 1u) != 0u) {
    ++even_h;
  }

  printf("Suggestions:\n");

  if (((even_w != (uint64_t)width) || (even_h != (uint64_t)height)) && (even_w <= (uint64_t)UINT32_MAX) && (even_h <= (uint64_t)UINT32_MAX)) {
    printf("  - nearest-even: %" PRIu64 "x%" PRIu64 "\n", even_w, even_h);
  }

  needed_w = hv_ceil_div_u64(slice_bytes, (uint64_t)height);
  if ((needed_w & 1u) != 0u) {
    ++needed_w;
  }
  if ((needed_w > 0u) && (needed_w <= (uint64_t)UINT32_MAX)) {
    printf("  - fit-by-width: %" PRIu64 "x%u\n", needed_w, height);
  }

  needed_h = hv_ceil_div_u64(slice_bytes, (uint64_t)width);
  if ((needed_h & 1u) != 0u) {
    ++needed_h;
  }
  if ((needed_h > 0u) && (needed_h <= (uint64_t)UINT32_MAX)) {
    printf("  - fit-by-height: %ux%" PRIu64 "\n", width, needed_h);
  }
}

static void hv_print_hilbert_dry_run(
  uint64_t slice_bytes,
  int auto_order,
  uint32_t requested_order,
  int paginate
)
{
  uint32_t order = 0u;
  uint32_t side = 0u;
  uint64_t capacity = 0u;
  uint64_t page_count = 1u;
  double utilization = 0.0;

  if (auto_order) {
    if (paginate && (slice_bytes > 0u)) {
      uint32_t page_order = 12u;
      uint32_t page_side = 0u;
      uint64_t page_capacity = 0u;

      if (page_order < HV_HILBERT_MIN_ORDER) {
        page_order = HV_HILBERT_MIN_ORDER;
      }
      if (page_order > HV_HILBERT_MAX_ORDER) {
        page_order = HV_HILBERT_MAX_ORDER;
      }

      if (
        hv_hilbert_capacity_for_order(page_order, &page_capacity) &&
        hv_hilbert_side_for_order(page_order, &page_side) &&
        (slice_bytes > page_capacity)
      ) {
        order = page_order;
        side = page_side;
        capacity = page_capacity;
      }
    }

    if ((capacity == 0u) && !hv_hilbert_pick_order(slice_bytes, &order, &side, &capacity)) {
      printf("Dry run failed: input slice (%" PRIu64 " bytes) exceeds max Hilbert capacity without pagination.\n", slice_bytes);
      return;
    }
  } else {
    order = requested_order;
    if (!hv_hilbert_side_for_order(order, &side) || !hv_hilbert_capacity_for_order(order, &capacity)) {
      printf("Dry run failed: invalid manual order %u.\n", requested_order);
      return;
    }
  }

  if (slice_bytes > 0u) {
    page_count = hv_ceil_div_u64(slice_bytes, capacity);
    utilization = ((double)((slice_bytes < capacity) ? slice_bytes : capacity) * 100.0) / (double)capacity;
  }

  printf("Dry run:\n");
  printf("  layout: hilbert\n");
  printf("  slice_bytes: %" PRIu64 "\n", slice_bytes);
  printf("  order: %u\n", order);
  printf("  dimensions: %ux%u\n", side, side);
  printf("  capacity_per_page: %" PRIu64 "\n", capacity);
  printf("  page_count: %" PRIu64 "\n", page_count);
  printf("  utilization_first_page: %.2f%%\n", utilization);
}

static void hv_print_rect_dry_run(
  uint64_t slice_bytes,
  uint32_t width,
  uint32_t height,
  int strict_adjacency
)
{
  uint64_t capacity = 0u;
  uint64_t page_count = 1u;
  double utilization = 0.0;
  int parity_warning = hv_rect_has_unavoidable_diagonal(width, height);

  if (!hv_mul_u64((uint64_t)width, (uint64_t)height, &capacity) || (capacity == 0u)) {
    printf("Dry run failed: dimension capacity overflow for %ux%u.\n", width, height);
    return;
  }

  if (slice_bytes > 0u) {
    page_count = hv_ceil_div_u64(slice_bytes, capacity);
    utilization = ((double)((slice_bytes < capacity) ? slice_bytes : capacity) * 100.0) / (double)capacity;
  }

  printf("Dry run:\n");
  printf("  layout: rect-hilbert\n");
  printf("  slice_bytes: %" PRIu64 "\n", slice_bytes);
  printf("  dimensions: %ux%u\n", width, height);
  printf("  capacity_per_page: %" PRIu64 "\n", capacity);
  printf("  page_count: %" PRIu64 "\n", page_count);
  printf("  utilization_first_page: %.2f%%\n", utilization);

  if (parity_warning) {
    printf("  warning: odd/even parity may require a diagonal step in 2D rectangular traversal.\n");
  }
  if (strict_adjacency && parity_warning) {
    printf("  strict-adjacency: REJECTED (choose parity-safe dimensions).\n");
  }

  hv_print_rect_suggestions(slice_bytes, width, height);
}

int main(int argc, char **argv)
{
  static const struct option long_options[] = {
    {"output", required_argument, 0, 'o'},
    {"order", required_argument, 0, 'n'},
    {"auto-order", no_argument, 0, 'a'},
    {"offset", required_argument, 0, 'f'},
    {"length", required_argument, 0, 'l'},
    {"paginate", no_argument, 0, 'p'},
    {"legend", no_argument, 0, 'g'},
    {"legend-path", required_argument, 0, 'G'},
    {"layout", required_argument, 0, HV_OPT_LAYOUT},
    {"dimensions", required_argument, 0, HV_OPT_DIMENSIONS},
    {"dry-run", no_argument, 0, HV_OPT_DRY_RUN},
    {"strict-adjacency", no_argument, 0, HV_OPT_STRICT_ADJACENCY},
    {"help", no_argument, 0, 'h'},
    {0, 0, 0, 0}
  };

  const char *input_path = 0;
  const char *output_path = 0;
  const char *legend_path = 0;
  char *default_legend_path = 0;
  uint64_t offset = 0u;
  uint64_t length = 0u;
  int has_length = 0;
  int auto_order = 1;
  int paginate = 0;
  int legend_enabled = 0;
  uint32_t order = 0u;
  int layout = HV_LAYOUT_HILBERT;
  int dimensions_set = 0;
  uint32_t width = 0u;
  uint32_t height = 0u;
  int dry_run = 0;
  int strict_adjacency = 0;
  HvRenderOptions options;
  HvRenderResult result;
  char err[512];
  int opt = 0;

  while ((opt = getopt_long(argc, argv, "o:n:af:l:pgG:h", long_options, 0)) != -1) {
    switch (opt) {
      case 'o':
        output_path = optarg;
        break;
      case 'n':
        if (!hv_parse_order(optarg, &order)) {
          fprintf(
            stderr,
            "Invalid order '%s' (expected %u..%u)\n",
            optarg,
            HV_HILBERT_MIN_ORDER,
            HV_HILBERT_MAX_ORDER
          );
          return 1;
        }
        auto_order = 0;
        break;
      case 'a':
        auto_order = 1;
        break;
      case 'f':
        if (!hv_parse_u64(optarg, &offset)) {
          fprintf(stderr, "Invalid offset '%s'\n", optarg);
          return 1;
        }
        break;
      case 'l':
        if (!hv_parse_u64(optarg, &length)) {
          fprintf(stderr, "Invalid length '%s'\n", optarg);
          return 1;
        }
        has_length = 1;
        break;
      case 'p':
        paginate = 1;
        break;
      case 'g':
        legend_enabled = 1;
        break;
      case 'G':
        legend_enabled = 1;
        legend_path = optarg;
        break;
      case HV_OPT_LAYOUT:
        if (!hv_parse_layout(optarg, &layout)) {
          fprintf(stderr, "Invalid layout '%s' (expected 'hilbert' or 'rect-hilbert')\n", optarg);
          return 1;
        }
        break;
      case HV_OPT_DIMENSIONS:
        if (!hv_parse_dimensions(optarg, &width, &height)) {
          fprintf(stderr, "Invalid dimensions '%s' (expected <W>x<H> with positive integers)\n", optarg);
          return 1;
        }
        dimensions_set = 1;
        break;
      case HV_OPT_DRY_RUN:
        dry_run = 1;
        break;
      case HV_OPT_STRICT_ADJACENCY:
        strict_adjacency = 1;
        break;
      case 'h':
        hv_print_usage(argv[0]);
        return 0;
      default:
        hv_print_usage(argv[0]);
        return 1;
    }
  }

  if ((argc - optind) != 1) {
    hv_print_usage(argv[0]);
    return 1;
  }
  input_path = argv[optind];

  if (output_path == 0) {
    fprintf(stderr, "Missing required --output path\n");
    hv_print_usage(argv[0]);
    return 1;
  }

  if (layout == HV_LAYOUT_RECT_HILBERT) {
    if (!dimensions_set) {
      fprintf(stderr, "Layout 'rect-hilbert' requires --dimensions <W>x<H>\n");
      return 1;
    }
    if (strict_adjacency && hv_rect_has_unavoidable_diagonal(width, height)) {
      fprintf(
        stderr,
        "Strict adjacency rejected %ux%u: odd larger side with even smaller side requires a diagonal step\n",
        width,
        height
      );
      return 1;
    }
  } else {
    if (dimensions_set) {
      fprintf(stderr, "--dimensions is only supported with --layout rect-hilbert\n");
      return 1;
    }
    if (strict_adjacency) {
      fprintf(stderr, "--strict-adjacency is only supported with --layout rect-hilbert\n");
      return 1;
    }
  }

  if (legend_enabled && (legend_path == 0)) {
    if (!hv_make_default_legend_path(output_path, &default_legend_path)) {
      fprintf(stderr, "Failed to allocate default legend path\n");
      return 1;
    }
    legend_path = default_legend_path;
  }

  if (dry_run) {
    uint64_t slice_bytes = 0u;

    memset(err, 0, sizeof(err));
    if (!hv_compute_slice_bytes(input_path, offset, has_length, length, &slice_bytes, err, sizeof(err))) {
      fprintf(stderr, "Dry run failed: %s\n", err);
      free(default_legend_path);
      return 1;
    }

    if (layout == HV_LAYOUT_RECT_HILBERT) {
      hv_print_rect_dry_run(slice_bytes, width, height, strict_adjacency);
    } else {
      hv_print_hilbert_dry_run(slice_bytes, auto_order, order, paginate);
    }

    free(default_legend_path);
    return 0;
  }

  memset(&options, 0, sizeof(options));
  memset(&result, 0, sizeof(result));
  memset(err, 0, sizeof(err));

  options.input_path = input_path;
  options.output_path = output_path;
  options.legend_path = legend_path;
  options.offset = offset;
  options.has_length = has_length;
  options.length = length;
  options.auto_order = auto_order;
  options.order = order;
  options.paginate = paginate;
  options.legend_enabled = legend_enabled;
  options.layout = layout;
  options.dimensions_set = dimensions_set;
  options.width = width;
  options.height = height;
  options.strict_adjacency = strict_adjacency;

  if (!hv_render_file(&options, &result, err, sizeof(err))) {
    fprintf(stderr, "Render failed: %s\n", err);
    free(default_legend_path);
    return 1;
  }

  if (layout == HV_LAYOUT_RECT_HILBERT) {
    if (result.page_count <= 1u) {
      printf(
        "Wrote %s (layout=rect-hilbert, dimensions=%ux%u, input-bytes=%" PRIu64 ", capacity=%" PRIu64 ")\n",
        output_path,
        width,
        height,
        result.input_bytes,
        result.capacity
      );
    } else {
      printf(
        "Wrote %" PRIu64 " pages based on %s (layout=rect-hilbert, dimensions=%ux%u, input-bytes=%" PRIu64 ", capacity/page=%" PRIu64 ")\n",
        result.page_count,
        output_path,
        width,
        height,
        result.input_bytes,
        result.capacity
      );
    }
  } else if (result.page_count <= 1u) {
    printf(
      "Wrote %s (order=%u, side=%u, input-bytes=%" PRIu64 ", capacity=%" PRIu64 ")\n",
      output_path,
      result.order,
      result.side,
      result.input_bytes,
      result.capacity
    );
  } else {
    printf(
      "Wrote %" PRIu64 " pages based on %s (order=%u, side=%u, input-bytes=%" PRIu64 ", capacity/page=%" PRIu64 ")\n",
      result.page_count,
      output_path,
      result.order,
      result.side,
      result.input_bytes,
      result.capacity
    );
  }

  if (legend_enabled && (legend_path != 0)) {
    printf("Wrote legend %s\n", legend_path);
  }

  free(default_legend_path);
  return 0;
}
