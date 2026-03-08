#ifndef HV_3D_PLATFORM_H
#define HV_3D_PLATFORM_H

#include "3d_camera.h"
#include "3d_renderer.h"
#include "byte_cube.h"
#include "point_cloud3d.h"

#include <stddef.h>

typedef enum Hv3DByteCubeControl {
  HV_3D_BYTE_CUBE_CONTROL_BLEND_ACCUMULATE = 0,
  HV_3D_BYTE_CUBE_CONTROL_BLEND_ALPHA = 1,
  HV_3D_BYTE_CUBE_CONTROL_CYCLE_PALETTE = 2,
  HV_3D_BYTE_CUBE_CONTROL_BRIGHTNESS_STANDARD = 3,
  HV_3D_BYTE_CUBE_CONTROL_BRIGHTNESS_LOW = 4,
  HV_3D_BYTE_CUBE_CONTROL_BRIGHTNESS_INCREASE = 5,
  HV_3D_BYTE_CUBE_CONTROL_BRIGHTNESS_DECREASE = 6,
  HV_3D_BYTE_CUBE_CONTROL_CONTRAST_INCREASE = 7,
  HV_3D_BYTE_CUBE_CONTROL_CONTRAST_DECREASE = 8,
  HV_3D_BYTE_CUBE_CONTROL_INTENSITY_NEAREST = 9,
  HV_3D_BYTE_CUBE_CONTROL_INTENSITY_LINEAR = 10,
  HV_3D_BYTE_CUBE_CONTROL_POSITION_NEAREST = 11,
  HV_3D_BYTE_CUBE_CONTROL_POSITION_LINEAR = 12,
  HV_3D_BYTE_CUBE_CONTROL_RESET = 13
} Hv3DByteCubeControl;

int hv_3d_platform_viewer_requested(void);
int hv_3d_platform_viewer_available(void);
const char *hv_3d_platform_viewer_support_text(void);
int hv_3d_platform_apply_byte_cube_control(Hv3DByteCubeViewSettings *settings, Hv3DByteCubeControl control);
int hv_3d_platform_render_static_cloud(
  const HvPointCloud3D *cloud,
  const Hv3DCamera *camera,
  float point_size,
  char *err,
  size_t err_size
);
int hv_3d_platform_render_static_byte_cube(
  const HvByteCube3D *cube,
  const Hv3DCamera *camera,
  const Hv3DByteCubeViewSettings *settings,
  char *err,
  size_t err_size
);

#endif
