#include "3d_renderer.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define HV_PRINTF_LIKE(fmt_idx, first_arg_idx) __attribute__((format(printf, fmt_idx, first_arg_idx)))
#else
#define HV_PRINTF_LIKE(fmt_idx, first_arg_idx)
#endif

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

int hv_3d_renderer_write_point_cloud_summary(
  FILE *stream,
  const HvPointCloud3D *cloud,
  const Hv3DCamera *camera,
  char *err,
  size_t err_size
)
{
  (void)camera;

  if ((stream == 0) || (cloud == 0)) {
    hv_set_error(err, err_size, "invalid arguments for 3D renderer summary");
    return 0;
  }

  if (fprintf(
        stream,
        "Loaded point cloud: points=%zu order=%u side=%u capacity=%" PRIu64 "\n",
        cloud->count,
        cloud->order,
        cloud->side,
        cloud->capacity
      ) < 0) {
    hv_set_error(err, err_size, "failed to write 3D point cloud summary: %s", strerror(errno));
    return 0;
  }

  if (cloud->count > 0u) {
    const HvPoint3D *first = &cloud->points[0];
    const HvPoint3D *last = &cloud->points[cloud->count - 1u];

    if (
      (fprintf(
         stream,
         "First point: %.6f %.6f %.6f rgb=%u,%u,%u\n",
         (double)first->x,
         (double)first->y,
         (double)first->z,
         first->r,
         first->g,
         first->b
       ) < 0) ||
      (fprintf(
         stream,
         "Last point: %.6f %.6f %.6f rgb=%u,%u,%u\n",
         (double)last->x,
         (double)last->y,
         (double)last->z,
         last->r,
         last->g,
         last->b
       ) < 0)
    ) {
      hv_set_error(err, err_size, "failed to write 3D point cloud detail: %s", strerror(errno));
      return 0;
    }
  }

  return 1;
}
