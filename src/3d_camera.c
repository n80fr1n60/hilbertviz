#include "3d_camera.h"

#include <math.h>

static const float HV_3D_CAMERA_PITCH_MIN_DEGREES = -85.0f;
static const float HV_3D_CAMERA_PITCH_MAX_DEGREES = 85.0f;
static const float HV_3D_CAMERA_DISTANCE_MIN = 0.20f;
static const float HV_3D_CAMERA_DISTANCE_MAX = 24.0f;
static const float HV_3D_CAMERA_ORBIT_DEGREES_PER_PIXEL = 0.30f;
static const float HV_3D_CAMERA_ZOOM_FRACTION_PER_WHEEL = 0.12f;
static const float HV_3D_CAMERA_ZOOM_MIN_STEP = 0.05f;
static const float HV_3D_CAMERA_BYTE_CUBE_OVERVIEW_PAD = 1.35f;
static const float HV_3D_CAMERA_BYTE_CUBE_OVERVIEW_MIN_DISTANCE = 2.5f;

static float hv_normalize_byte_cube_coord(uint8_t coord)
{
  return ((((float)coord + 0.5f) / (float)HV_BYTE_CUBE_SIDE) * 2.0f) - 1.0f;
}

static uint32_t hv_3d_camera_shorter_side(uint32_t width, uint32_t height)
{
  if (width == 0u) {
    width = 1u;
  }
  if (height == 0u) {
    height = 1u;
  }
  return (width < height) ? width : height;
}

static int hv_3d_camera_fit_sphere(Hv3DCamera *camera, float center_x, float center_y, float center_z, float radius)
{
  float aspect = 1.0f;
  float top = 0.75f;
  float right = 0.0f;
  float min_half_extent = top;
  float fitted_distance = 0.0f;

  if (camera == 0) {
    return 0;
  }

  (void)hv_3d_camera_set_viewport(camera, camera->viewport_width, camera->viewport_height);
  camera->target_x = center_x;
  camera->target_y = center_y;
  camera->target_z = center_z;

  if (radius <= 0.0f) {
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

  fitted_distance = (radius * 1.25f) / min_half_extent;
  if (fitted_distance < 0.9f) {
    fitted_distance = 0.9f;
  }

  camera->distance = fitted_distance;
  return hv_3d_camera_clamp_distance(camera);
}

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
  if ((camera == 0) || (cloud == 0)) {
    return 0;
  }

  return hv_3d_camera_fit_sphere(
    camera,
    cloud->bounds.center_x,
    cloud->bounds.center_y,
    cloud->bounds.center_z,
    ((cloud->count == 0u) ? 0.0f : cloud->bounds.radius)
  );
}

int hv_3d_camera_fit_byte_cube(Hv3DCamera *camera, const HvByteCube3D *cube)
{
  float min_x = 0.0f;
  float min_y = 0.0f;
  float min_z = 0.0f;
  float max_x = 0.0f;
  float max_y = 0.0f;
  float max_z = 0.0f;
  float center_x = 0.0f;
  float center_y = 0.0f;
  float center_z = 0.0f;
  float half_x = 0.0f;
  float half_y = 0.0f;
  float half_z = 0.0f;
  float radius = 0.0f;

  if ((camera == 0) || (cube == 0)) {
    return 0;
  }
  if ((cube->side != HV_BYTE_CUBE_SIDE) || (cube->total_voxels != HV_BYTE_CUBE_TOTAL_VOXELS)) {
    return 0;
  }
  if (cube->occupied_voxels == 0u) {
    return hv_3d_camera_fit_sphere(camera, 0.0f, 0.0f, 0.0f, 0.0f);
  }

  min_x = hv_normalize_byte_cube_coord(cube->occupied_min_x);
  min_y = hv_normalize_byte_cube_coord(cube->occupied_min_y);
  min_z = hv_normalize_byte_cube_coord(cube->occupied_min_z);
  max_x = hv_normalize_byte_cube_coord(cube->occupied_max_x);
  max_y = hv_normalize_byte_cube_coord(cube->occupied_max_y);
  max_z = hv_normalize_byte_cube_coord(cube->occupied_max_z);

  center_x = (min_x + max_x) * 0.5f;
  center_y = (min_y + max_y) * 0.5f;
  center_z = (min_z + max_z) * 0.5f;
  half_x = (max_x - min_x) * 0.5f;
  half_y = (max_y - min_y) * 0.5f;
  half_z = (max_z - min_z) * 0.5f;
  radius = sqrtf((half_x * half_x) + (half_y * half_y) + (half_z * half_z));

  return hv_3d_camera_fit_sphere(camera, center_x, center_y, center_z, radius);
}

int hv_3d_camera_fit_byte_cube_overview(Hv3DCamera *camera, const HvByteCube3D *cube)
{
  if ((camera == 0) || (cube == 0)) {
    return 0;
  }
  if (!hv_3d_camera_fit_byte_cube(camera, cube)) {
    return 0;
  }
  if (cube->occupied_voxels == 0u) {
    return 1;
  }

  camera->distance *= HV_3D_CAMERA_BYTE_CUBE_OVERVIEW_PAD;
  if (camera->distance < HV_3D_CAMERA_BYTE_CUBE_OVERVIEW_MIN_DISTANCE) {
    camera->distance = HV_3D_CAMERA_BYTE_CUBE_OVERVIEW_MIN_DISTANCE;
  }
  return hv_3d_camera_clamp_distance(camera);
}

int hv_3d_camera_preserve_scale_on_resize(Hv3DCamera *camera, uint32_t new_width, uint32_t new_height)
{
  uint32_t old_side = 0u;
  uint32_t new_side = 0u;

  if (camera == 0) {
    return 0;
  }

  old_side = hv_3d_camera_shorter_side(camera->viewport_width, camera->viewport_height);
  new_side = hv_3d_camera_shorter_side(new_width, new_height);
  if (old_side == 0u) {
    old_side = 1u;
  }
  if (new_side == 0u) {
    new_side = 1u;
  }

  camera->distance *= (float)new_side / (float)old_side;
  if (!hv_3d_camera_clamp_distance(camera)) {
    return 0;
  }
  return hv_3d_camera_set_viewport(camera, new_width, new_height);
}
