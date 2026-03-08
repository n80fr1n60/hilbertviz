#include "3d_camera.h"

#include <math.h>

static const float HV_3D_CAMERA_PITCH_MIN_DEGREES = -85.0f;
static const float HV_3D_CAMERA_PITCH_MAX_DEGREES = 85.0f;
static const float HV_3D_CAMERA_DISTANCE_MIN = 0.75f;
static const float HV_3D_CAMERA_DISTANCE_MAX = 24.0f;
static const float HV_3D_CAMERA_ORBIT_DEGREES_PER_PIXEL = 0.30f;
static const float HV_3D_CAMERA_ZOOM_FRACTION_PER_WHEEL = 0.12f;
static const float HV_3D_CAMERA_ZOOM_MIN_STEP = 0.25f;

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

int hv_3d_camera_set_viewport(Hv3DCamera *camera, uint32_t width, uint32_t height)
{
  if (camera == 0) {
    return 0;
  }

  camera->viewport_width = (width > 0u) ? width : 1u;
  camera->viewport_height = (height > 0u) ? height : 1u;
  return 1;
}

int hv_3d_camera_clamp_pitch(Hv3DCamera *camera)
{
  if (camera == 0) {
    return 0;
  }

  if (camera->pitch_degrees < HV_3D_CAMERA_PITCH_MIN_DEGREES) {
    camera->pitch_degrees = HV_3D_CAMERA_PITCH_MIN_DEGREES;
  } else if (camera->pitch_degrees > HV_3D_CAMERA_PITCH_MAX_DEGREES) {
    camera->pitch_degrees = HV_3D_CAMERA_PITCH_MAX_DEGREES;
  }

  return 1;
}

int hv_3d_camera_clamp_distance(Hv3DCamera *camera)
{
  if (camera == 0) {
    return 0;
  }

  if (camera->distance < HV_3D_CAMERA_DISTANCE_MIN) {
    camera->distance = HV_3D_CAMERA_DISTANCE_MIN;
  } else if (camera->distance > HV_3D_CAMERA_DISTANCE_MAX) {
    camera->distance = HV_3D_CAMERA_DISTANCE_MAX;
  }

  return 1;
}

int hv_3d_camera_orbit(Hv3DCamera *camera, float delta_x, float delta_y)
{
  if (camera == 0) {
    return 0;
  }

  camera->yaw_degrees += delta_x * HV_3D_CAMERA_ORBIT_DEGREES_PER_PIXEL;
  camera->pitch_degrees += delta_y * HV_3D_CAMERA_ORBIT_DEGREES_PER_PIXEL;
  return hv_3d_camera_clamp_pitch(camera);
}

int hv_3d_camera_zoom(Hv3DCamera *camera, float wheel_delta)
{
  float zoom_step = 0.0f;

  if (camera == 0) {
    return 0;
  }

  zoom_step = camera->distance * HV_3D_CAMERA_ZOOM_FRACTION_PER_WHEEL;
  if (zoom_step < HV_3D_CAMERA_ZOOM_MIN_STEP) {
    zoom_step = HV_3D_CAMERA_ZOOM_MIN_STEP;
  }

  camera->distance -= wheel_delta * zoom_step;
  return hv_3d_camera_clamp_distance(camera);
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

  (void)hv_3d_camera_set_viewport(camera, camera->viewport_width, camera->viewport_height);

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
  return hv_3d_camera_clamp_distance(camera);
}
