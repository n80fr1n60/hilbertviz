#include "hilbert.h"
#include "render.h"

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void hv_print_usage(const char *prog)
{
  fprintf(
    stderr,
    "Usage: %s <input.bin> -o <output.{ppm|png}> [options]\n"
    "Options:\n"
    "  -o, --output <path>     Output image path (required, .ppm or .png)\n"
    "  -n, --order <N>         Hilbert order (%u..%u)\n"
    "  -a, --auto-order        Auto-pick smallest order to fit data (default)\n"
    "  -f, --offset <bytes>    Read input starting at offset\n"
    "  -l, --length <bytes>    Read only this many bytes from offset\n"
    "  -p, --paginate          Emit multiple pages when input exceeds one image\n"
    "  -g, --legend            Write sidecar legend stats file (default: <output>.legend.txt)\n"
    "  -G, --legend-path <p>   Explicit legend output path\n"
    "  -h, --help              Show this help\n",
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

static char *hv_make_default_legend_path(const char *output_path)
{
  static const char suffix[] = ".legend.txt";
  size_t out_len = 0u;
  size_t total_len = 0u;
  char *path = 0;

  if (output_path == 0) {
    return 0;
  }

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
  return path;
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

  if (legend_enabled && (legend_path == 0)) {
    default_legend_path = hv_make_default_legend_path(output_path);
    if (default_legend_path == 0) {
      fprintf(stderr, "Failed to allocate default legend path\n");
      return 1;
    }
    legend_path = default_legend_path;
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

  if (!hv_render_file(&options, &result, err, sizeof(err))) {
    fprintf(stderr, "Render failed: %s\n", err);
    free(default_legend_path);
    return 1;
  }

  if (result.page_count <= 1u) {
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
