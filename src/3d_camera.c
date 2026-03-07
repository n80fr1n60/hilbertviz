#include "3d_camera.h"

void hv_3d_camera_init_defaults(Hv3DCamera *camera)
{
  if (camera == 0) {
    return;
  }

  camera->yaw_degrees = 35.0f;
  camera->pitch_degrees = 25.0f;
  camera->distance = 3.0f;
  camera->viewport_width = 1280u;
  camera->viewport_height = 720u;
}
