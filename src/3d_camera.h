#ifndef HV_3D_CAMERA_H
#define HV_3D_CAMERA_H

#include <stdint.h>

typedef struct Hv3DCamera {
  float yaw_degrees;
  float pitch_degrees;
  float distance;
  uint32_t viewport_width;
  uint32_t viewport_height;
} Hv3DCamera;

void hv_3d_camera_init_defaults(Hv3DCamera *camera);

#endif
