#include "3d_camera.h"

#include <stddef.h>

void hv_3d_camera_init_defaults(Hv3DCamera *camera)
{
  if (camera == 0) {
    return;
  }

  camera->yaw_degrees = 35.0f;
  camera->pitch_degrees = 25.0f;
  camera->distance = 3.0f;
  camera->target_x = 0.0f;
  camera->target_y = 0.0f;
  camera->target_z = 0.0f;
  camera->viewport_width = 1280u;
  camera->viewport_height = 720u;
}

int hv_3d_camera_fit_cloud(Hv3DCamera *camera, const HvPointCloud3D *cloud)
{
  float aspect = 1.0f;
  float top = 0.75f;
  float right = top;
  float min_half_extent = top;
  float fitted_distance = 0.0f;

  if ((camera == 0) || (cloud == 0)) {
    return 0;
  }

  camera->target_x = cloud->bounds.center_x;
  camera->target_y = cloud->bounds.center_y;
  camera->target_z = cloud->bounds.center_z;

  if ((cloud->count == 0u) || (cloud->bounds.radius <= 0.0f)) {
    return 1;
  }

  if (camera->viewport_height > 0u) {
    aspect = (float)camera->viewport_width / (float)camera->viewport_height;
  }
  right = top * aspect;
  if ((right > 0.0f) && (right < min_half_extent)) {
    min_half_extent = right;
  }
  if (min_half_extent <= 0.0f) {
    min_half_extent = top;
  }

  fitted_distance = (cloud->bounds.radius * 1.25f) / min_half_extent;
  if (fitted_distance < 1.5f) {
    fitted_distance = 1.5f;
  }

  camera->distance = fitted_distance;
  return 1;
}
