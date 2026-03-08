#include "3d_renderer.h"

#if defined(HV_3D_VIEWER_AVAILABLE)
#define GL_GLEXT_PROTOTYPES

#include <SDL2/SDL.h>
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
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

#if defined(HV_3D_VIEWER_AVAILABLE)
static PFNGLGENBUFFERSPROC hv_glGenBuffers_ptr = 0;
static PFNGLBINDBUFFERPROC hv_glBindBuffer_ptr = 0;
static PFNGLBUFFERDATAPROC hv_glBufferData_ptr = 0;
static PFNGLDELETEBUFFERSPROC hv_glDeleteBuffers_ptr = 0;
#endif

int hv_3d_renderer_init(
  Hv3DRenderer *renderer,
  const HvPointCloud3D *cloud,
  char *err,
  size_t err_size
)
{
#if defined(HV_3D_VIEWER_AVAILABLE)
  size_t bytes = 0u;

  if ((renderer == 0) || (cloud == 0)) {
    hv_set_error(err, err_size, "invalid arguments for 3D renderer initialization");
    return 0;
  }

  renderer->vertex_buffer = 0u;
  renderer->vertex_count = cloud->count;

  if (
    (hv_glGenBuffers_ptr == 0) ||
    (hv_glBindBuffer_ptr == 0) ||
    (hv_glBufferData_ptr == 0) ||
    (hv_glDeleteBuffers_ptr == 0)
  ) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpedantic"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
    hv_glGenBuffers_ptr = (PFNGLGENBUFFERSPROC)SDL_GL_GetProcAddress("glGenBuffers");
    hv_glBindBuffer_ptr = (PFNGLBINDBUFFERPROC)SDL_GL_GetProcAddress("glBindBuffer");
    hv_glBufferData_ptr = (PFNGLBUFFERDATAPROC)SDL_GL_GetProcAddress("glBufferData");
    hv_glDeleteBuffers_ptr = (PFNGLDELETEBUFFERSPROC)SDL_GL_GetProcAddress("glDeleteBuffers");
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    if (
      (hv_glGenBuffers_ptr == 0) ||
      (hv_glBindBuffer_ptr == 0) ||
      (hv_glBufferData_ptr == 0) ||
      (hv_glDeleteBuffers_ptr == 0)
    ) {
      hv_set_error(err, err_size, "required OpenGL buffer functions are unavailable");
      return 0;
    }
  }

  if (cloud->count == 0u) {
    return 1;
  }

  if (cloud->count > (SIZE_MAX / sizeof(*cloud->points))) {
    hv_set_error(err, err_size, "3D renderer buffer size overflow");
    return 0;
  }
  bytes = cloud->count * sizeof(*cloud->points);

  hv_glGenBuffers_ptr(1, &renderer->vertex_buffer);
  if (renderer->vertex_buffer == 0u) {
    hv_set_error(err, err_size, "failed to allocate OpenGL vertex buffer");
    return 0;
  }

  hv_glBindBuffer_ptr(GL_ARRAY_BUFFER, renderer->vertex_buffer);
  hv_glBufferData_ptr(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, cloud->points, GL_STATIC_DRAW);
  hv_glBindBuffer_ptr(GL_ARRAY_BUFFER, 0u);
  return 1;
#else
  if ((renderer == 0) || (cloud == 0)) {
    hv_set_error(err, err_size, "invalid arguments for 3D renderer initialization");
    return 0;
  }

  hv_set_error(err, err_size, "3D renderer is unavailable in this build");
  return 0;
#endif
}

int hv_3d_renderer_validate_point_size(float point_size, char *err, size_t err_size)
{
  if ((point_size < HV_3D_POINT_SIZE_MIN) || (point_size > HV_3D_POINT_SIZE_MAX)) {
    hv_set_error(
      err,
      err_size,
      "invalid 3D point size %.3f (supported range: %.1f..%.1f)",
      (double)point_size,
      (double)HV_3D_POINT_SIZE_MIN,
      (double)HV_3D_POINT_SIZE_MAX
    );
    return 0;
  }

  return 1;
}

void hv_3d_renderer_shutdown(Hv3DRenderer *renderer)
{
#if defined(HV_3D_VIEWER_AVAILABLE)
  if (renderer == 0) {
    return;
  }
  if ((renderer->vertex_buffer != 0u) && (hv_glDeleteBuffers_ptr != 0)) {
    hv_glDeleteBuffers_ptr(1, &renderer->vertex_buffer);
  }
  renderer->vertex_buffer = 0u;
  renderer->vertex_count = 0u;
#else
  (void)renderer;
#endif
}

int hv_3d_renderer_draw(
  const Hv3DRenderer *renderer,
  const Hv3DCamera *camera,
  uint32_t viewport_width,
  uint32_t viewport_height,
  float point_size,
  char *err,
  size_t err_size
)
{
#if defined(HV_3D_VIEWER_AVAILABLE)
  double aspect = 1.0;
  double near_plane = 1.0;
  double far_plane = 10.0;
  double top = 0.75 * near_plane;
  double right = top;

  if ((renderer == 0) || (camera == 0)) {
    hv_set_error(err, err_size, "invalid arguments for 3D renderer draw");
    return 0;
  }
  if (!hv_3d_renderer_validate_point_size(point_size, err, err_size)) {
    return 0;
  }

  glViewport(0, 0, (GLsizei)viewport_width, (GLsizei)viewport_height);
  glClearColor(0.78f, 0.78f, 0.80f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glPointSize(point_size);

  if (viewport_height == 0u) {
    viewport_height = 1u;
  }
  aspect = (double)viewport_width / (double)viewport_height;
  right = top * aspect;

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glFrustum(-right, right, -top, top, near_plane, far_plane);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glTranslatef(0.0f, 0.0f, -camera->distance);
  glRotatef(camera->pitch_degrees, 1.0f, 0.0f, 0.0f);
  glRotatef(camera->yaw_degrees, 0.0f, 1.0f, 0.0f);
  glTranslatef(-camera->target_x, -camera->target_y, -camera->target_z);

  if ((renderer->vertex_count > 0u) && (renderer->vertex_buffer != 0u)) {
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    hv_glBindBuffer_ptr(GL_ARRAY_BUFFER, renderer->vertex_buffer);
    glVertexPointer(3, GL_FLOAT, (GLsizei)sizeof(HvPoint3D), (const void *)offsetof(HvPoint3D, x));
    glColorPointer(3, GL_UNSIGNED_BYTE, (GLsizei)sizeof(HvPoint3D), (const void *)offsetof(HvPoint3D, r));
    glDrawArrays(GL_POINTS, 0, (GLsizei)renderer->vertex_count);
    hv_glBindBuffer_ptr(GL_ARRAY_BUFFER, 0u);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
  }

  return 1;
#else
  (void)viewport_width;
  (void)viewport_height;
  if (!hv_3d_renderer_validate_point_size(point_size, err, err_size)) {
    return 0;
  }
  if ((renderer == 0) || (camera == 0)) {
    hv_set_error(err, err_size, "invalid arguments for 3D renderer draw");
    return 0;
  }

  hv_set_error(err, err_size, "3D renderer is unavailable in this build");
  return 0;
#endif
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
