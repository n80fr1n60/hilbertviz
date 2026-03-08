#ifndef HV_3D_RENDERER_H
#define HV_3D_RENDERER_H

#include "3d_camera.h"
#include "byte_cube.h"
#include "point_cloud3d.h"

#include <stddef.h>
#include <stdio.h>

#define HV_3D_POINT_SIZE_DEFAULT 2.0f
#define HV_3D_POINT_SIZE_MIN 1.0f
#define HV_3D_POINT_SIZE_MAX 64.0f

typedef enum Hv3DByteCubePalette {
  HV_3D_BYTE_CUBE_PALETTE_RGB = 0,
  HV_3D_BYTE_CUBE_PALETTE_HEAT = 1,
  HV_3D_BYTE_CUBE_PALETTE_ASCII = 2,
  HV_3D_BYTE_CUBE_PALETTE_MONO = 3
} Hv3DByteCubePalette;

typedef enum Hv3DByteCubeBlendMode {
  HV_3D_BYTE_CUBE_BLEND_ACCUMULATE = 0,
  HV_3D_BYTE_CUBE_BLEND_ALPHA = 1
} Hv3DByteCubeBlendMode;

typedef enum Hv3DByteCubeProjection {
  HV_3D_BYTE_CUBE_PROJECTION_FREE_3D = 0,
  HV_3D_BYTE_CUBE_PROJECTION_XY = 1,
  HV_3D_BYTE_CUBE_PROJECTION_XZ = 2,
  HV_3D_BYTE_CUBE_PROJECTION_YZ = 3
} Hv3DByteCubeProjection;

typedef enum Hv3DByteCubeInterpolation {
  HV_3D_BYTE_CUBE_INTERPOLATION_LINEAR = 0,
  HV_3D_BYTE_CUBE_INTERPOLATION_NEAREST = 1
} Hv3DByteCubeInterpolation;

typedef struct Hv3DByteCubeViewSettings {
  float brightness;
  float contrast;
  Hv3DByteCubePalette palette;
  Hv3DByteCubeBlendMode blend_mode;
  Hv3DByteCubeProjection projection;
  Hv3DByteCubeInterpolation interpolation;
  Hv3DByteCubeInterpolation position_interpolation;
} Hv3DByteCubeViewSettings;

const char *hv_3d_byte_cube_palette_name(Hv3DByteCubePalette palette);
int hv_3d_byte_cube_parse_palette(const char *text, Hv3DByteCubePalette *palette_out);
const char *hv_3d_byte_cube_blend_mode_name(Hv3DByteCubeBlendMode blend_mode);
int hv_3d_byte_cube_parse_blend_mode(const char *text, Hv3DByteCubeBlendMode *blend_mode_out);
const char *hv_3d_byte_cube_projection_name(Hv3DByteCubeProjection projection);
int hv_3d_byte_cube_parse_projection(const char *text, Hv3DByteCubeProjection *projection_out);
const char *hv_3d_byte_cube_interpolation_name(Hv3DByteCubeInterpolation interpolation);
int hv_3d_byte_cube_parse_interpolation(const char *text, Hv3DByteCubeInterpolation *interpolation_out);
int hv_3d_byte_cube_projection_view(
  Hv3DByteCubeProjection projection,
  int *use_free_camera_out,
  float *pitch_degrees_out,
  float *yaw_degrees_out
);
int hv_3d_byte_cube_is_printable_trigram(float x, float y, float z);
int hv_3d_byte_cube_palette_color(
  Hv3DByteCubePalette palette,
  float x,
  float y,
  float z,
  float density,
  float *r_out,
  float *g_out,
  float *b_out
);
float hv_3d_byte_cube_volume_alpha_scale(float zoom, Hv3DByteCubeBlendMode blend_mode);

typedef struct Hv3DRenderer {
  float tex_min_x;
  float tex_min_y;
  float tex_min_z;
  float tex_max_x;
  float tex_max_y;
  float tex_max_z;
  float projection_zoom;
  unsigned int projection_texture_xy;
  unsigned int projection_texture_xz;
  unsigned int projection_texture_yz;
  unsigned int volume_texture_intensity;
  unsigned int volume_texture_position;
  unsigned int volume_program;
  int volume_uniform_intensity;
  int volume_uniform_position;
  int volume_uniform_brightness;
  int volume_uniform_contrast;
  int volume_uniform_palette;
  int volume_uniform_depth;
  int volume_uniform_alpha_scale;
  int volume_uniform_view;
  int volume_uniform_aspect;
  int volume_uniform_bounds_min;
  int volume_uniform_bounds_max;
  int volume_uniform_geom_min;
  int volume_uniform_geom_max;
  unsigned int vertex_buffer;
  unsigned int mode;
  size_t vertex_count;
  uint32_t volume_side;
} Hv3DRenderer;

int hv_3d_renderer_write_point_cloud_summary(
  FILE *stream,
  const HvPointCloud3D *cloud,
  const Hv3DCamera *camera,
  char *err,
  size_t err_size
);
int hv_3d_renderer_init(
  Hv3DRenderer *renderer,
  const HvPointCloud3D *cloud,
  char *err,
  size_t err_size
);
void hv_3d_byte_cube_view_settings_init_defaults(Hv3DByteCubeViewSettings *settings);
int hv_3d_byte_cube_view_settings_validate(
  const Hv3DByteCubeViewSettings *settings,
  char *err,
  size_t err_size
);
int hv_3d_renderer_init_byte_cube(
  Hv3DRenderer *renderer,
  const HvByteCube3D *cube,
  const Hv3DByteCubeViewSettings *settings,
  char *err,
  size_t err_size
);
float hv_3d_byte_cube_clamp_unit(float value);
float hv_3d_renderer_byte_cube_local_coord(float texcoord);
float hv_3d_byte_cube_density_normalize(uint32_t count, uint32_t max_density);
float hv_3d_byte_cube_apply_brightness(float value, float brightness);
float hv_3d_byte_cube_apply_contrast(float value, float contrast);
float hv_3d_byte_cube_density_transfer(
  uint32_t count,
  uint32_t max_density,
  float brightness,
  float contrast
);
unsigned int hv_3d_byte_cube_volume_layers(float zoom);
int hv_3d_renderer_center_square_viewport(
  uint32_t viewport_width,
  uint32_t viewport_height,
  int *x_out,
  int *y_out,
  uint32_t *side_out
);
uint8_t hv_3d_renderer_byte_cube_alpha(
  uint32_t count,
  uint32_t max_density,
  const Hv3DByteCubeViewSettings *settings
);
int hv_3d_renderer_validate_point_size(float point_size, char *err, size_t err_size);
void hv_3d_renderer_shutdown(Hv3DRenderer *renderer);
int hv_3d_renderer_draw(
  const Hv3DRenderer *renderer,
  const Hv3DCamera *camera,
  uint32_t viewport_width,
  uint32_t viewport_height,
  float point_size,
  char *err,
  size_t err_size
);
int hv_3d_renderer_draw_byte_cube(
  const Hv3DRenderer *renderer,
  const Hv3DCamera *camera,
  const Hv3DByteCubeViewSettings *settings,
  uint32_t viewport_width,
  uint32_t viewport_height,
  char *err,
  size_t err_size
);

#endif
