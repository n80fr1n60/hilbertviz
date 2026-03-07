#ifndef HV_3D_PLATFORM_H
#define HV_3D_PLATFORM_H

#include "3d_camera.h"
#include "point_cloud3d.h"

#include <stddef.h>

int hv_3d_platform_viewer_requested(void);
int hv_3d_platform_viewer_available(void);
const char *hv_3d_platform_viewer_support_text(void);
int hv_3d_platform_render_static_cloud(
  const HvPointCloud3D *cloud,
  const Hv3DCamera *camera,
  char *err,
  size_t err_size
);

#endif
