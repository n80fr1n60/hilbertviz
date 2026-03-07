#ifndef HV_3D_RENDERER_H
#define HV_3D_RENDERER_H

#include "3d_camera.h"
#include "point_cloud3d.h"

#include <stddef.h>
#include <stdio.h>

int hv_3d_renderer_write_point_cloud_summary(
  FILE *stream,
  const HvPointCloud3D *cloud,
  const Hv3DCamera *camera,
  char *err,
  size_t err_size
);

#endif
