#ifndef HV_3D_CAMERA_H
#define HV_3D_CAMERA_H

#include "point_cloud3d.h"

#include <stdint.h>

typedef struct Hv3DCamera {
  float yaw_degrees;
  float pitch_degrees;
  float distance;
  float target_x;
  float target_y;
  float target_z;
  uint32_t viewport_width;
  uint32_t viewport_height;
} Hv3DCamera;

void hv_3d_camera_init_defaults(Hv3DCamera *camera);
int hv_3d_camera_fit_cloud(Hv3DCamera *camera, const HvPointCloud3D *cloud);

#endif
