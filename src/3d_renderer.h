#ifndef HV_3D_RENDERER_H
#define HV_3D_RENDERER_H

#include "3d_camera.h"
#include "point_cloud3d.h"

#include <stddef.h>
#include <stdio.h>

typedef struct Hv3DRenderer {
  unsigned int vertex_buffer;
  size_t vertex_count;
} Hv3DRenderer;

int hv_3d_renderer_write_point_cloud_summary(
  FILE *stream,
  const HvPointCloud3D *cloud,
  const Hv3DCamera *camera,
  char *err,
  size_t err_size
);
int hv_3d_renderer_init(
  Hv3DRenderer *renderer,
  const HvPointCloud3D *cloud,
  char *err,
  size_t err_size
);
void hv_3d_renderer_shutdown(Hv3DRenderer *renderer);
int hv_3d_renderer_draw(
  const Hv3DRenderer *renderer,
  const Hv3DCamera *camera,
  uint32_t viewport_width,
  uint32_t viewport_height,
  char *err,
  size_t err_size
);

#endif
