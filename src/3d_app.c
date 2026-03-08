#include "3d_app.h"

#include "byte_cube.h"
#include "3d_camera.h"
#include "3d_mode.h"
#include "3d_platform.h"
#include "3d_renderer.h"
#include "hilbert3d.h"
#include "point_cloud3d.h"

#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  HV3D_OPT_ORDER = 1000,
  HV3D_OPT_MODE,
  HV3D_OPT_OFFSET,
  HV3D_OPT_LENGTH,
  HV3D_OPT_POINT_SIZE,
  HV3D_OPT_BRIGHTNESS,
  HV3D_OPT_CONTRAST,
  HV3D_OPT_PALETTE,
  HV3D_OPT_BLEND_MODE,
  HV3D_OPT_PROJECTION,
  HV3D_OPT_INTERPOLATION
};

static void hv3d_print_usage(const char *prog)
{
  (void)printf(
    "Usage: %s <input> [--mode <hilbert|byte-cube>] [--order <n>] [--offset <bytes>] [--length <bytes>] [--point-size <pixels>] [--brightness <value>] [--contrast <value>] [--palette <rgb|heat|ascii|mono>] [--blend-mode <accumulate|alpha>] [--projection <free-3d|xy|xz|yz>] [--interpolation <linear|nearest>] [--help]\n"
    "\n"
    "Experimental 3D binary visualization pipeline.\n"
    "Current default mode: hilbert.\n"
    "Current status:\n"
    "  - hilbert: 3D Hilbert point-cloud summary plus viewer\n"
    "  - byte-cube: trigram density-volume summary plus slice viewer\n"
    "Byte-cube controls:\n"
    "  --brightness <value>\n"
    "  --contrast <value>\n"
    "  --palette <rgb|heat|ascii|mono>\n"
    "  --blend-mode <accumulate|alpha>\n"
    "  --projection <free-3d|xy|xz|yz>\n"
    "  --interpolation <linear|nearest>\n"
    "Interactive viewer support: %s\n"
    "Supported order range: %u..%u\n",
    prog,
    hv_3d_platform_viewer_support_text(),
    HV_HILBERT3D_MIN_ORDER,
    HV_HILBERT3D_MAX_ORDER
  );
}

static int hv3d_parse_u64_decimal_strict(const char *text, uint64_t *out)
{
  const unsigned char *p = 0;
  unsigned long long parsed = 0u;
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

static int hv3d_parse_order(const char *text, uint32_t *order_out)
{
  uint64_t parsed = 0u;

  if ((text == 0) || (order_out == 0)) {
    return 0;
  }
  if (!hv3d_parse_u64_decimal_strict(text, &parsed)) {
    return 0;
  }
  if ((parsed < HV_HILBERT3D_MIN_ORDER) || (parsed > HV_HILBERT3D_MAX_ORDER)) {
    return 0;
  }

  *order_out = (uint32_t)parsed;
  return 1;
}

static int hv3d_parse_point_size(const char *text, float *point_size_out)
{
  char *end = 0;
  double parsed = 0.0;

  if ((text == 0) || (point_size_out == 0) || (*text == '\0')) {
    return 0;
  }

  errno = 0;
  parsed = strtod(text, &end);
  if ((errno != 0) || (end == text) || (*end != '\0') || !isfinite(parsed)) {
    return 0;
  }
  if ((parsed < (double)HV_3D_POINT_SIZE_MIN) || (parsed > (double)HV_3D_POINT_SIZE_MAX)) {
    return 0;
  }

  *point_size_out = (float)parsed;
  return 1;
}

static int hv3d_parse_float_strict(const char *text, float *out)
{
  char *end = 0;
  double parsed = 0.0;

  if ((text == 0) || (out == 0) || (*text == '\0')) {
    return 0;
  }

  errno = 0;
  parsed = strtod(text, &end);
  if ((errno != 0) || (end == text) || (*end != '\0') || !isfinite(parsed)) {
    return 0;
  }

  *out = (float)parsed;
  return 1;
}

static int hv3d_skip_viewer(void)
{
  const char *value = getenv("HILBERTVIZ3D_SKIP_VIEWER");

  if (value == 0) {
    return 0;
  }

  return
    (strcmp(value, "1") == 0) ||
    (strcmp(value, "true") == 0) ||
    (strcmp(value, "TRUE") == 0) ||
    (strcmp(value, "yes") == 0) ||
    (strcmp(value, "YES") == 0);
}

int hv_3d_run_app(int argc, char **argv)
{
  static const struct option long_options[] = {
    {"order", required_argument, 0, HV3D_OPT_ORDER},
    {"mode", required_argument, 0, HV3D_OPT_MODE},
    {"offset", required_argument, 0, HV3D_OPT_OFFSET},
    {"length", required_argument, 0, HV3D_OPT_LENGTH},
    {"point-size", required_argument, 0, HV3D_OPT_POINT_SIZE},
    {"brightness", required_argument, 0, HV3D_OPT_BRIGHTNESS},
    {"contrast", required_argument, 0, HV3D_OPT_CONTRAST},
    {"palette", required_argument, 0, HV3D_OPT_PALETTE},
    {"blend-mode", required_argument, 0, HV3D_OPT_BLEND_MODE},
    {"projection", required_argument, 0, HV3D_OPT_PROJECTION},
    {"interpolation", required_argument, 0, HV3D_OPT_INTERPOLATION},
    {"help", no_argument, 0, 'h'},
    {0, 0, 0, 0}
  };
  const char *input_path = 0;
  Hv3DMode mode = HV_3D_MODE_HILBERT;
  uint32_t order = 0u;
  uint64_t offset = 0u;
  uint64_t length = 0u;
  int has_length = 0;
  int have_order = 0;
  int have_brightness = 0;
  int have_contrast = 0;
  int have_palette = 0;
  int have_blend_mode = 0;
  int have_projection = 0;
  int have_interpolation = 0;
  float point_size = HV_3D_POINT_SIZE_DEFAULT;
  HvByteCube3D byte_cube;
  Hv3DByteCubeViewSettings byte_cube_settings;
  HvPointCloud3D cloud;
  Hv3DCamera camera;
  char err[512];
  int opt = 0;

  memset(&byte_cube, 0, sizeof(byte_cube));
  memset(&byte_cube_settings, 0, sizeof(byte_cube_settings));
  memset(&cloud, 0, sizeof(cloud));
  memset(&camera, 0, sizeof(camera));
  memset(err, 0, sizeof(err));
  hv_3d_byte_cube_view_settings_init_defaults(&byte_cube_settings);

  while ((opt = getopt_long(argc, argv, "h", long_options, 0)) != -1) {
    switch (opt) {
      case HV3D_OPT_MODE:
        if (!hv_3d_parse_mode(optarg, &mode)) {
          (void)fprintf(stderr, "Invalid 3D mode '%s'\n", optarg);
          return 1;
        }
        break;
      case HV3D_OPT_ORDER:
        if (!hv3d_parse_order(optarg, &order)) {
          (void)fprintf(stderr, "Invalid 3D order '%s'\n", optarg);
          return 1;
        }
        have_order = 1;
        break;
      case HV3D_OPT_OFFSET:
        if (!hv3d_parse_u64_decimal_strict(optarg, &offset)) {
          (void)fprintf(stderr, "Invalid offset '%s'\n", optarg);
          return 1;
        }
        break;
      case HV3D_OPT_LENGTH:
        if (!hv3d_parse_u64_decimal_strict(optarg, &length)) {
          (void)fprintf(stderr, "Invalid length '%s'\n", optarg);
          return 1;
        }
        has_length = 1;
        break;
      case HV3D_OPT_POINT_SIZE:
        if (!hv3d_parse_point_size(optarg, &point_size)) {
          (void)fprintf(
            stderr,
            "Invalid point size '%s' (supported range: %.1f..%.1f)\n",
            optarg,
            (double)HV_3D_POINT_SIZE_MIN,
            (double)HV_3D_POINT_SIZE_MAX
          );
          return 1;
        }
        break;
      case HV3D_OPT_BRIGHTNESS:
        if (!hv3d_parse_float_strict(optarg, &byte_cube_settings.brightness)) {
          (void)fprintf(stderr, "Invalid brightness '%s'\n", optarg);
          return 1;
        }
        have_brightness = 1;
        break;
      case HV3D_OPT_CONTRAST:
        if (!hv3d_parse_float_strict(optarg, &byte_cube_settings.contrast)) {
          (void)fprintf(stderr, "Invalid contrast '%s'\n", optarg);
          return 1;
        }
        have_contrast = 1;
        break;
      case HV3D_OPT_PALETTE:
        if (!hv_3d_byte_cube_parse_palette(optarg, &byte_cube_settings.palette)) {
          (void)fprintf(stderr, "Invalid byte-cube palette '%s'\n", optarg);
          return 1;
        }
        have_palette = 1;
        break;
      case HV3D_OPT_BLEND_MODE:
        if (!hv_3d_byte_cube_parse_blend_mode(optarg, &byte_cube_settings.blend_mode)) {
          (void)fprintf(stderr, "Invalid byte-cube blend mode '%s'\n", optarg);
          return 1;
        }
        have_blend_mode = 1;
        break;
      case HV3D_OPT_PROJECTION:
        if (!hv_3d_byte_cube_parse_projection(optarg, &byte_cube_settings.projection)) {
          (void)fprintf(stderr, "Invalid byte-cube projection '%s'\n", optarg);
          return 1;
        }
        have_projection = 1;
        break;
      case HV3D_OPT_INTERPOLATION:
        if (!hv_3d_byte_cube_parse_interpolation(optarg, &byte_cube_settings.interpolation)) {
          (void)fprintf(stderr, "Invalid byte-cube interpolation '%s'\n", optarg);
          return 1;
        }
        byte_cube_settings.position_interpolation = byte_cube_settings.interpolation;
        have_interpolation = 1;
        break;
      case 'h':
        hv3d_print_usage(argv[0]);
        return 0;
      default:
        hv3d_print_usage(argv[0]);
        return 1;
    }
  }

  if (optind >= argc) {
    (void)fprintf(stderr, "hilbertviz3d: missing input path\n");
    return 1;
  }
  input_path = argv[optind];
  if ((optind + 1) != argc) {
    (void)fprintf(stderr, "hilbertviz3d: only one input path is supported\n");
    return 1;
  }
  if ((mode != HV_3D_MODE_HILBERT) && have_order) {
    (void)fprintf(stderr, "hilbertviz3d: --order is only supported with --mode hilbert\n");
    return 1;
  }
  if ((mode == HV_3D_MODE_HILBERT) && !have_order) {
    (void)fprintf(stderr, "hilbertviz3d: missing required --order\n");
    return 1;
  }
  if ((mode != HV_3D_MODE_BYTE_CUBE) && have_brightness) {
    (void)fprintf(stderr, "hilbertviz3d: --brightness is only supported with --mode byte-cube\n");
    return 1;
  }
  if ((mode != HV_3D_MODE_BYTE_CUBE) && have_contrast) {
    (void)fprintf(stderr, "hilbertviz3d: --contrast is only supported with --mode byte-cube\n");
    return 1;
  }
  if ((mode != HV_3D_MODE_BYTE_CUBE) && have_palette) {
    (void)fprintf(stderr, "hilbertviz3d: --palette is only supported with --mode byte-cube\n");
    return 1;
  }
  if ((mode != HV_3D_MODE_BYTE_CUBE) && have_blend_mode) {
    (void)fprintf(stderr, "hilbertviz3d: --blend-mode is only supported with --mode byte-cube\n");
    return 1;
  }
  if ((mode != HV_3D_MODE_BYTE_CUBE) && have_projection) {
    (void)fprintf(stderr, "hilbertviz3d: --projection is only supported with --mode byte-cube\n");
    return 1;
  }
  if ((mode != HV_3D_MODE_BYTE_CUBE) && have_interpolation) {
    (void)fprintf(stderr, "hilbertviz3d: --interpolation is only supported with --mode byte-cube\n");
    return 1;
  }
  if (mode == HV_3D_MODE_BYTE_CUBE) {
    if (!hv_3d_byte_cube_view_settings_validate(&byte_cube_settings, err, sizeof(err))) {
      (void)fprintf(stderr, "hilbertviz3d failed: %s\n", err);
      return 1;
    }
    if (!hv_build_byte_cube3d(input_path, offset, has_length, length, &byte_cube, err, sizeof(err))) {
      (void)fprintf(stderr, "hilbertviz3d failed: %s\n", err);
      return 1;
    }
    if (!hv_write_byte_cube3d_summary(stdout, &byte_cube, err, sizeof(err))) {
      hv_free_byte_cube3d(&byte_cube);
      (void)fprintf(stderr, "hilbertviz3d failed: %s\n", err);
      return 1;
    }
    hv_3d_camera_init_defaults(&camera);
    if (!hv_3d_camera_fit_byte_cube_overview(&camera, &byte_cube)) {
      hv_free_byte_cube3d(&byte_cube);
      (void)fprintf(stderr, "hilbertviz3d failed: failed to fit initial byte-cube overview camera\n");
      return 1;
    }
    if (hv_3d_platform_viewer_available() && !hv3d_skip_viewer()) {
      if (!hv_3d_platform_render_static_byte_cube(&byte_cube, &camera, &byte_cube_settings, err, sizeof(err))) {
        hv_free_byte_cube3d(&byte_cube);
        (void)fprintf(stderr, "hilbertviz3d failed: %s\n", err);
        return 1;
      }
    }
    hv_free_byte_cube3d(&byte_cube);
    return 0;
  }

  if (!hv_build_point_cloud3d(input_path, order, offset, has_length, length, &cloud, err, sizeof(err))) {
    (void)fprintf(stderr, "hilbertviz3d failed: %s\n", err);
    return 1;
  }

  hv_3d_camera_init_defaults(&camera);
  if (!hv_3d_camera_fit_cloud(&camera, &cloud)) {
    hv_free_point_cloud3d(&cloud);
    (void)fprintf(stderr, "hilbertviz3d failed: failed to fit initial 3D camera\n");
    return 1;
  }
  if (!hv_3d_renderer_write_point_cloud_summary(stdout, &cloud, &camera, err, sizeof(err))) {
    hv_free_point_cloud3d(&cloud);
    (void)fprintf(stderr, "hilbertviz3d failed: %s\n", err);
    return 1;
  }

  if (hv_3d_platform_viewer_available() && !hv3d_skip_viewer()) {
    if (!hv_3d_platform_render_static_cloud(&cloud, &camera, point_size, err, sizeof(err))) {
      hv_free_point_cloud3d(&cloud);
      (void)fprintf(stderr, "hilbertviz3d failed: %s\n", err);
      return 1;
    }
  }

  hv_free_point_cloud3d(&cloud);
  return 0;
}
