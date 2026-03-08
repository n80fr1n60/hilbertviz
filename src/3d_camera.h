#ifndef HV_3D_CAMERA_H
#define HV_3D_CAMERA_H

#include "byte_cube.h"
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
int hv_3d_camera_set_viewport(Hv3DCamera *camera, uint32_t width, uint32_t height);
int hv_3d_camera_clamp_pitch(Hv3DCamera *camera);
int hv_3d_camera_clamp_distance(Hv3DCamera *camera);
int hv_3d_camera_orbit(Hv3DCamera *camera, float delta_x, float delta_y);
int hv_3d_camera_zoom(Hv3DCamera *camera, float wheel_delta);
int hv_3d_camera_fit_cloud(Hv3DCamera *camera, const HvPointCloud3D *cloud);
int hv_3d_camera_fit_byte_cube(Hv3DCamera *camera, const HvByteCube3D *cube);
int hv_3d_camera_fit_byte_cube_overview(Hv3DCamera *camera, const HvByteCube3D *cube);
int hv_3d_camera_preserve_scale_on_resize(Hv3DCamera *camera, uint32_t new_width, uint32_t new_height);

#endif
