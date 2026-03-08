#include "3d_renderer.h"

#if defined(HV_3D_VIEWER_AVAILABLE)
#define GL_GLEXT_PROTOTYPES

#include <SDL2/SDL.h>
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define HV_PRINTF_LIKE(fmt_idx, first_arg_idx) __attribute__((format(printf, fmt_idx, first_arg_idx)))
#else
#define HV_PRINTF_LIKE(fmt_idx, first_arg_idx)
#endif

enum {
  HV_3D_RENDERER_MODE_NONE = 0,
  HV_3D_RENDERER_MODE_POINTS = 1,
  HV_3D_RENDERER_MODE_BYTE_CUBE = 2
};

static void HV_PRINTF_LIKE(3, 4) hv_set_error(char *err, size_t err_size, const char *fmt, ...)
{
  va_list args;

  if ((err == 0) || (err_size == 0u)) {
    return;
  }

  va_start(args, fmt);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
  (void)vsnprintf(err, err_size, fmt, args);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
  va_end(args);
}

#if defined(HV_3D_VIEWER_AVAILABLE)
static PFNGLGENBUFFERSPROC hv_glGenBuffers_ptr = 0;
static PFNGLBINDBUFFERPROC hv_glBindBuffer_ptr = 0;
static PFNGLBUFFERDATAPROC hv_glBufferData_ptr = 0;
static PFNGLDELETEBUFFERSPROC hv_glDeleteBuffers_ptr = 0;

static const char hv_3d_volume_vertex_shader_src[] =
  "#version 120\n"
  "uniform float u_depth;\n"
  "uniform mat4 u_view;\n"
  "uniform float u_aspect;\n"
  "uniform vec3 u_bounds_min;\n"
  "uniform vec3 u_bounds_max;\n"
  "varying vec3 v_texcoord;\n"
  "void main(void) {\n"
  "  vec4 sample_point = u_view * vec4(gl_Vertex.x * u_aspect, gl_Vertex.y, (u_depth * 2.0) - 1.0, 1.0);\n"
  "  v_texcoord = (sample_point.xyz * 0.5) + vec3(0.5);\n"
  "  gl_Position = vec4(gl_Vertex.xy, 0.0, 1.0);\n"
  "}\n";

static const char hv_3d_volume_fragment_shader_src[] =
  "#version 120\n"
  "uniform sampler3D u_intensity_tex;\n"
  "uniform sampler3D u_position_tex;\n"
  "uniform float u_brightness;\n"
  "uniform float u_contrast;\n"
  "uniform float u_alpha_scale;\n"
  "uniform int u_palette;\n"
  "uniform vec3 u_bounds_min;\n"
  "uniform vec3 u_bounds_max;\n"
  "varying vec3 v_texcoord;\n"
  "float clamp_unit(float v) {\n"
  "  return clamp(v, 0.0, 1.0);\n"
  "}\n"
  "int printable_trigram(vec3 pos) {\n"
  "  int votes = 0;\n"
  "  if ((pos.x >= (32.0 / 255.0)) && (pos.x <= (126.0 / 255.0))) votes++;\n"
  "  if ((pos.y >= (32.0 / 255.0)) && (pos.y <= (126.0 / 255.0))) votes++;\n"
  "  if ((pos.z >= (32.0 / 255.0)) && (pos.z <= (126.0 / 255.0))) votes++;\n"
  "  return (votes >= 2) ? 1 : 0;\n"
  "}\n"
  "vec3 palette_color(int palette, vec3 pos, float density) {\n"
  "  vec3 rgb;\n"
  "  if (palette == 0) {\n"
  "    rgb = vec3(0.22) + (vec3(0.78) * pow(pos, vec3(0.45)));\n"
  "  } else if (palette == 1) {\n"
  "    rgb = vec3(\n"
  "      clamp_unit(0.25 + (1.10 * density)),\n"
  "      clamp_unit((density - 0.18) * 1.35),\n"
  "      clamp_unit((density - 0.70) * 2.60)\n"
  "    );\n"
  "  } else if (palette == 2) {\n"
  "    if (printable_trigram(pos) != 0) {\n"
  "      rgb = vec3(\n"
  "        clamp_unit(0.65 + (0.35 * density)),\n"
  "        clamp_unit(0.45 + (0.45 * density)),\n"
  "        clamp_unit(0.10 + (0.15 * density))\n"
  "      );\n"
  "    } else {\n"
  "      rgb = vec3(\n"
  "        clamp_unit(0.15 + (0.20 * density)),\n"
  "        clamp_unit(0.28 + (0.30 * density)),\n"
  "        clamp_unit(0.45 + (0.40 * density))\n"
  "      );\n"
  "    }\n"
  "  } else {\n"
  "    rgb = vec3(0.20 + (0.80 * density));\n"
  "  }\n"
  "  return clamp(rgb, vec3(0.0), vec3(1.0));\n"
  "}\n"
  "void main(void) {\n"
  "  float density = 0.0;\n"
  "  vec3 pos = vec3(0.5);\n"
  "  if ((v_texcoord.x < 0.0) || (v_texcoord.y < 0.0) || (v_texcoord.z < 0.0) ||\n"
  "      (v_texcoord.x > 1.0) || (v_texcoord.y > 1.0) || (v_texcoord.z > 1.0)) {\n"
  "    discard;\n"
  "  }\n"
  "  if ((v_texcoord.x < u_bounds_min.x) || (v_texcoord.y < u_bounds_min.y) || (v_texcoord.z < u_bounds_min.z) ||\n"
  "      (v_texcoord.x > u_bounds_max.x) || (v_texcoord.y > u_bounds_max.y) || (v_texcoord.z > u_bounds_max.z)) {\n"
  "    discard;\n"
  "  }\n"
  "  density = texture3D(u_intensity_tex, v_texcoord).r;\n"
  "  pos = texture3D(u_position_tex, v_texcoord).rgb;\n"
  "  density = clamp_unit(density + u_brightness);\n"
  "  density = clamp_unit(((density - 0.5) * u_contrast) + 0.5);\n"
  "  density = clamp_unit(density * u_alpha_scale);\n"
  "  if (density <= 0.001) {\n"
  "    discard;\n"
  "  }\n"
  "  gl_FragColor = vec4(palette_color(u_palette, pos, density), density);\n"
  "}\n";
#endif

#if defined(HV_3D_VIEWER_AVAILABLE)
static int hv_3d_renderer_load_buffer_functions(char *err, size_t err_size)
{
  if (
    (hv_glGenBuffers_ptr != 0) &&
    (hv_glBindBuffer_ptr != 0) &&
    (hv_glBufferData_ptr != 0) &&
    (hv_glDeleteBuffers_ptr != 0)
  ) {
    return 1;
  }
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpedantic"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
  hv_glGenBuffers_ptr = (PFNGLGENBUFFERSPROC)SDL_GL_GetProcAddress("glGenBuffers");
  hv_glBindBuffer_ptr = (PFNGLBINDBUFFERPROC)SDL_GL_GetProcAddress("glBindBuffer");
  hv_glBufferData_ptr = (PFNGLBUFFERDATAPROC)SDL_GL_GetProcAddress("glBufferData");
  hv_glDeleteBuffers_ptr = (PFNGLDELETEBUFFERSPROC)SDL_GL_GetProcAddress("glDeleteBuffers");
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
  if (
    (hv_glGenBuffers_ptr == 0) ||
    (hv_glBindBuffer_ptr == 0) ||
    (hv_glBufferData_ptr == 0) ||
    (hv_glDeleteBuffers_ptr == 0)
  ) {
    hv_set_error(err, err_size, "required OpenGL buffer functions are unavailable");
    return 0;
  }
  return 1;
}

static int hv_3d_renderer_upload_projection_texture(
  unsigned int *texture,
  const uint8_t *rgba,
  const Hv3DByteCubeViewSettings *settings,
  char *err,
  size_t err_size
)
{
  if ((texture == 0) || (rgba == 0) || (settings == 0)) {
    hv_set_error(err, err_size, "invalid projection texture upload arguments");
    return 0;
  }

  glGenTextures(1, texture);
  if (*texture == 0u) {
    hv_set_error(err, err_size, "failed to allocate OpenGL 2D projection texture");
    return 0;
  }

  glBindTexture(GL_TEXTURE_2D, *texture);
  if (settings->interpolation == HV_3D_BYTE_CUBE_INTERPOLATION_NEAREST) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  } else {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(
    GL_TEXTURE_2D,
    0,
    GL_RGBA8,
    (GLsizei)HV_BYTE_CUBE_SIDE,
    (GLsizei)HV_BYTE_CUBE_SIDE,
    0,
    GL_RGBA,
    GL_UNSIGNED_BYTE,
    rgba
  );
  glBindTexture(GL_TEXTURE_2D, 0u);
  return 1;
}

static GLuint hv_3d_renderer_compile_shader(GLenum type, const char *source, char *err, size_t err_size)
{
  GLuint shader = 0u;
  GLint compiled = 0;

  shader = glCreateShader(type);
  if (shader == 0u) {
    hv_set_error(err, err_size, "failed to create OpenGL shader");
    return 0u;
  }

  glShaderSource(shader, 1, &source, 0);
  glCompileShader(shader);
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (compiled == GL_FALSE) {
    GLchar logbuf[1024];
    GLsizei loglen = 0;

    memset(logbuf, 0, sizeof(logbuf));
    glGetShaderInfoLog(shader, (GLsizei)sizeof(logbuf), &loglen, logbuf);
    hv_set_error(
      err,
      err_size,
      "OpenGL shader compilation failed: %.*s",
      (int)((loglen > 0) ? loglen : 0),
      logbuf
    );
    glDeleteShader(shader);
    return 0u;
  }

  return shader;
}

static int hv_3d_renderer_link_volume_program(Hv3DRenderer *renderer, char *err, size_t err_size)
{
  GLuint vertex_shader = 0u;
  GLuint fragment_shader = 0u;
  GLint linked = 0;

  if (renderer == 0) {
    hv_set_error(err, err_size, "invalid volume-program arguments");
    return 0;
  }

  vertex_shader = hv_3d_renderer_compile_shader(GL_VERTEX_SHADER, hv_3d_volume_vertex_shader_src, err, err_size);
  if (vertex_shader == 0u) {
    return 0;
  }
  fragment_shader = hv_3d_renderer_compile_shader(GL_FRAGMENT_SHADER, hv_3d_volume_fragment_shader_src, err, err_size);
  if (fragment_shader == 0u) {
    glDeleteShader(vertex_shader);
    return 0;
  }

  renderer->volume_program = glCreateProgram();
  if (renderer->volume_program == 0u) {
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    hv_set_error(err, err_size, "failed to create OpenGL shader program");
    return 0;
  }

  glAttachShader(renderer->volume_program, vertex_shader);
  glAttachShader(renderer->volume_program, fragment_shader);
  glLinkProgram(renderer->volume_program);
  glGetProgramiv(renderer->volume_program, GL_LINK_STATUS, &linked);
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);
  if (linked == GL_FALSE) {
    GLchar logbuf[1024];
    GLsizei loglen = 0;

    memset(logbuf, 0, sizeof(logbuf));
    glGetProgramInfoLog(renderer->volume_program, (GLsizei)sizeof(logbuf), &loglen, logbuf);
    hv_set_error(
      err,
      err_size,
      "OpenGL shader link failed: %.*s",
      (int)((loglen > 0) ? loglen : 0),
      logbuf
    );
    glDeleteProgram(renderer->volume_program);
    renderer->volume_program = 0u;
    return 0;
  }

  renderer->volume_uniform_intensity = glGetUniformLocation(renderer->volume_program, "u_intensity_tex");
  renderer->volume_uniform_position = glGetUniformLocation(renderer->volume_program, "u_position_tex");
  renderer->volume_uniform_brightness = glGetUniformLocation(renderer->volume_program, "u_brightness");
  renderer->volume_uniform_contrast = glGetUniformLocation(renderer->volume_program, "u_contrast");
  renderer->volume_uniform_alpha_scale = glGetUniformLocation(renderer->volume_program, "u_alpha_scale");
  renderer->volume_uniform_palette = glGetUniformLocation(renderer->volume_program, "u_palette");
  renderer->volume_uniform_depth = glGetUniformLocation(renderer->volume_program, "u_depth");
  renderer->volume_uniform_view = glGetUniformLocation(renderer->volume_program, "u_view");
  renderer->volume_uniform_aspect = glGetUniformLocation(renderer->volume_program, "u_aspect");
  renderer->volume_uniform_bounds_min = glGetUniformLocation(renderer->volume_program, "u_bounds_min");
  renderer->volume_uniform_bounds_max = glGetUniformLocation(renderer->volume_program, "u_bounds_max");
  renderer->volume_uniform_geom_min = glGetUniformLocation(renderer->volume_program, "u_geom_min");
  renderer->volume_uniform_geom_max = glGetUniformLocation(renderer->volume_program, "u_geom_max");
  return 1;
}

static int hv_3d_renderer_upload_byte_cube_volume_texture_float(
  unsigned int *texture,
  const float *data,
  GLenum internal_format,
  GLenum format,
  const Hv3DByteCubeViewSettings *settings,
  char *err,
  size_t err_size
)
{
  if ((texture == 0) || (data == 0) || (settings == 0)) {
    hv_set_error(err, err_size, "invalid byte-cube volume upload arguments");
    return 0;
  }

  glGenTextures(1, texture);
  if (*texture == 0u) {
    hv_set_error(err, err_size, "failed to allocate OpenGL 3D texture");
    return 0;
  }

  glBindTexture(GL_TEXTURE_3D, *texture);
  if (settings->interpolation == HV_3D_BYTE_CUBE_INTERPOLATION_NEAREST) {
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  } else {
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  }
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage3D(
    GL_TEXTURE_3D,
    0,
    (GLint)internal_format,
    (GLsizei)HV_BYTE_CUBE_SIDE,
    (GLsizei)HV_BYTE_CUBE_SIDE,
    (GLsizei)HV_BYTE_CUBE_SIDE,
    0,
    format,
    GL_FLOAT,
    data
  );
  glBindTexture(GL_TEXTURE_3D, 0u);
  return 1;
}

static int hv_3d_renderer_upload_byte_cube_volume_texture_u8(
  unsigned int *texture,
  const uint8_t *data,
  const Hv3DByteCubeViewSettings *settings,
  char *err,
  size_t err_size
)
{
  if ((texture == 0) || (data == 0) || (settings == 0)) {
    hv_set_error(err, err_size, "invalid byte-cube position-volume upload arguments");
    return 0;
  }

  glGenTextures(1, texture);
  if (*texture == 0u) {
    hv_set_error(err, err_size, "failed to allocate OpenGL 3D position texture");
    return 0;
  }

  glBindTexture(GL_TEXTURE_3D, *texture);
  if (settings->position_interpolation == HV_3D_BYTE_CUBE_INTERPOLATION_NEAREST) {
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  } else {
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  }
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage3D(
    GL_TEXTURE_3D,
    0,
    GL_RGB,
    (GLsizei)HV_BYTE_CUBE_SIDE,
    (GLsizei)HV_BYTE_CUBE_SIDE,
    (GLsizei)HV_BYTE_CUBE_SIDE,
    0,
    GL_RGB,
    GL_UNSIGNED_BYTE,
    data
  );
  glBindTexture(GL_TEXTURE_3D, 0u);
  return 1;
}

static void hv_3d_renderer_set_byte_cube_texture_filters(
  const Hv3DRenderer *renderer,
  const Hv3DByteCubeViewSettings *settings
)
{
  GLint intensity_filter = (settings->interpolation == HV_3D_BYTE_CUBE_INTERPOLATION_NEAREST) ? GL_NEAREST : GL_LINEAR;
  GLint position_filter = (settings->position_interpolation == HV_3D_BYTE_CUBE_INTERPOLATION_NEAREST) ? GL_NEAREST : GL_LINEAR;

  if ((renderer == 0) || (settings == 0)) {
    return;
  }

  if (renderer->volume_texture_intensity != 0u) {
    glBindTexture(GL_TEXTURE_3D, renderer->volume_texture_intensity);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, intensity_filter);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, intensity_filter);
  }
  if (renderer->volume_texture_position != 0u) {
    glBindTexture(GL_TEXTURE_3D, renderer->volume_texture_position);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, position_filter);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, position_filter);
  }
  glBindTexture(GL_TEXTURE_3D, 0u);
}

static float hv_3d_renderer_camera_zoom(const Hv3DCamera *camera)
{
  float zoom = 1.0f;

  if (camera == 0) {
    return 1.0f;
  }

  zoom = 3.0f / camera->distance;
  if (zoom < 0.20f) {
    zoom = 0.20f;
  } else if (zoom > 12.0f) {
    zoom = 12.0f;
  }
  return zoom;
}

static void hv_3d_renderer_projection_texcoords(
  const Hv3DRenderer *renderer,
  Hv3DByteCubeProjection projection,
  float *u0,
  float *v0,
  float *u1,
  float *v1
)
{
  if ((renderer == 0) || (u0 == 0) || (v0 == 0) || (u1 == 0) || (v1 == 0)) {
    return;
  }

  switch (projection) {
    case HV_3D_BYTE_CUBE_PROJECTION_XY:
      *u0 = renderer->tex_min_y;
      *v0 = renderer->tex_min_x;
      *u1 = renderer->tex_max_y;
      *v1 = renderer->tex_max_x;
      break;
    case HV_3D_BYTE_CUBE_PROJECTION_XZ:
      *u0 = renderer->tex_min_z;
      *v0 = renderer->tex_min_x;
      *u1 = renderer->tex_max_z;
      *v1 = renderer->tex_max_x;
      break;
    case HV_3D_BYTE_CUBE_PROJECTION_YZ:
      *u0 = renderer->tex_min_z;
      *v0 = renderer->tex_min_y;
      *u1 = renderer->tex_max_z;
      *v1 = renderer->tex_max_y;
      break;
    default:
      *u0 = 0.0f;
      *v0 = 0.0f;
      *u1 = 1.0f;
      *v1 = 1.0f;
      break;
  }
}

static void hv_3d_renderer_draw_outline_segment(
  float x0,
  float y0,
  float z0,
  float x1,
  float y1,
  float z1
)
{
  glVertex3f(x0, y0, z0);
  glVertex3f(x1, y1, z1);
}

static void hv_3d_renderer_draw_byte_cube_outline(
  const Hv3DRenderer *renderer,
  const Hv3DCamera *camera
)
{
  float minx = 0.0f;
  float miny = 0.0f;
  float minz = 0.0f;
  float maxx = 0.0f;
  float maxy = 0.0f;
  float maxz = 0.0f;
  float axis_len_x = 0.0f;
  float axis_len_y = 0.0f;
  float axis_len_z = 0.0f;

  if ((renderer == 0) || (camera == 0)) {
    return;
  }

  minx = hv_3d_renderer_byte_cube_local_coord(renderer->tex_min_x);
  miny = hv_3d_renderer_byte_cube_local_coord(renderer->tex_min_y);
  minz = hv_3d_renderer_byte_cube_local_coord(renderer->tex_min_z);
  maxx = hv_3d_renderer_byte_cube_local_coord(renderer->tex_max_x);
  maxy = hv_3d_renderer_byte_cube_local_coord(renderer->tex_max_y);
  maxz = hv_3d_renderer_byte_cube_local_coord(renderer->tex_max_z);

  axis_len_x = (maxx - minx) * 0.18f;
  axis_len_y = (maxy - miny) * 0.18f;
  axis_len_z = (maxz - minz) * 0.18f;
  if (axis_len_x <= 0.0f) {
    axis_len_x = 0.08f;
  }
  if (axis_len_y <= 0.0f) {
    axis_len_y = 0.08f;
  }
  if (axis_len_z <= 0.0f) {
    axis_len_z = 0.08f;
  }

  glLineWidth(1.5f);
  glBegin(GL_LINES);
  glColor4f(0.70f, 0.72f, 0.76f, 0.90f);

  hv_3d_renderer_draw_outline_segment(minx, miny, minz, maxx, miny, minz);
  hv_3d_renderer_draw_outline_segment(minx, miny, minz, minx, maxy, minz);
  hv_3d_renderer_draw_outline_segment(minx, miny, minz, minx, miny, maxz);
  hv_3d_renderer_draw_outline_segment(maxx, maxy, maxz, minx, maxy, maxz);
  hv_3d_renderer_draw_outline_segment(maxx, maxy, maxz, maxx, miny, maxz);
  hv_3d_renderer_draw_outline_segment(maxx, maxy, maxz, maxx, maxy, minz);
  hv_3d_renderer_draw_outline_segment(maxx, miny, minz, maxx, maxy, minz);
  hv_3d_renderer_draw_outline_segment(maxx, miny, minz, maxx, miny, maxz);
  hv_3d_renderer_draw_outline_segment(minx, maxy, minz, maxx, maxy, minz);
  hv_3d_renderer_draw_outline_segment(minx, maxy, minz, minx, maxy, maxz);
  hv_3d_renderer_draw_outline_segment(minx, miny, maxz, maxx, miny, maxz);
  hv_3d_renderer_draw_outline_segment(minx, miny, maxz, minx, maxy, maxz);

  glColor4f(0.95f, 0.30f, 0.30f, 1.0f);
  hv_3d_renderer_draw_outline_segment(minx, miny, minz, minx + axis_len_x, miny, minz);
  glColor4f(0.30f, 0.95f, 0.30f, 1.0f);
  hv_3d_renderer_draw_outline_segment(minx, miny, minz, minx, miny + axis_len_y, minz);
  glColor4f(0.35f, 0.55f, 1.00f, 1.0f);
  hv_3d_renderer_draw_outline_segment(minx, miny, minz, minx, miny, minz + axis_len_z);
  glEnd();
}

static void hv_3d_renderer_matrix_identity(float out[16])
{
  size_t i = 0u;

  if (out == 0) {
    return;
  }
  for (i = 0u; i < 16u; ++i) {
    out[i] = 0.0f;
  }
  out[0] = 1.0f;
  out[5] = 1.0f;
  out[10] = 1.0f;
  out[15] = 1.0f;
}

static void hv_3d_renderer_matrix_multiply(float out[16], const float a[16], const float b[16])
{
  float tmp[16];
  size_t row = 0u;
  size_t col = 0u;
  size_t k = 0u;

  if ((out == 0) || (a == 0) || (b == 0)) {
    return;
  }

  for (col = 0u; col < 4u; ++col) {
    for (row = 0u; row < 4u; ++row) {
      float value = 0.0f;
      for (k = 0u; k < 4u; ++k) {
        value += a[(k * 4u) + row] * b[(col * 4u) + k];
      }
      tmp[(col * 4u) + row] = value;
    }
  }
  memcpy(out, tmp, sizeof(tmp));
}

static void hv_3d_renderer_matrix_translate(float out[16], float x, float y, float z)
{
  hv_3d_renderer_matrix_identity(out);
  if (out == 0) {
    return;
  }
  out[12] = x;
  out[13] = y;
  out[14] = z;
}

static void hv_3d_renderer_matrix_scale(float out[16], float x, float y, float z)
{
  hv_3d_renderer_matrix_identity(out);
  if (out == 0) {
    return;
  }
  out[0] = x;
  out[5] = y;
  out[10] = z;
}

static void hv_3d_renderer_matrix_rotate_x(float out[16], float degrees)
{
  float radians = 0.0f;
  float s = 0.0f;
  float c = 0.0f;

  hv_3d_renderer_matrix_identity(out);
  if (out == 0) {
    return;
  }

  radians = degrees * ((float)M_PI / 180.0f);
  s = sinf(radians);
  c = cosf(radians);
  out[5] = c;
  out[6] = s;
  out[9] = -s;
  out[10] = c;
}

static void hv_3d_renderer_matrix_rotate_y(float out[16], float degrees)
{
  float radians = 0.0f;
  float s = 0.0f;
  float c = 0.0f;

  hv_3d_renderer_matrix_identity(out);
  if (out == 0) {
    return;
  }

  radians = degrees * ((float)M_PI / 180.0f);
  s = sinf(radians);
  c = cosf(radians);
  out[0] = c;
  out[2] = -s;
  out[8] = s;
  out[10] = c;
}

static void hv_3d_renderer_build_byte_cube_view_matrix(const Hv3DCamera *camera, float zoom, float out[16])
{
  float translate[16];
  float rotate_x[16];
  float rotate_y[16];
  float scale[16];
  float tmp[16];
  float inv_zoom = 1.0f;

  if ((camera == 0) || (out == 0)) {
    return;
  }

  if (zoom > 1e-6f) {
    inv_zoom = 1.0f / zoom;
  }

  hv_3d_renderer_matrix_translate(translate, camera->target_x, camera->target_y, camera->target_z);
  hv_3d_renderer_matrix_rotate_y(rotate_y, -camera->yaw_degrees);
  hv_3d_renderer_matrix_rotate_x(rotate_x, -camera->pitch_degrees);
  hv_3d_renderer_matrix_scale(scale, inv_zoom, inv_zoom, inv_zoom);

  hv_3d_renderer_matrix_multiply(tmp, rotate_y, rotate_x);
  hv_3d_renderer_matrix_multiply(out, tmp, scale);
  hv_3d_renderer_matrix_multiply(tmp, translate, out);
  memcpy(out, tmp, sizeof(tmp));
}
#endif

#if defined(HV_3D_VIEWER_AVAILABLE)
static void hv_3d_renderer_reset(Hv3DRenderer *renderer)
{
  if (renderer == 0) {
    return;
  }
  renderer->tex_min_x = 0.0f;
  renderer->tex_min_y = 0.0f;
  renderer->tex_min_z = 0.0f;
  renderer->tex_max_x = 1.0f;
  renderer->tex_max_y = 1.0f;
  renderer->tex_max_z = 1.0f;
  renderer->projection_zoom = 1.0f;
  renderer->projection_texture_xy = 0u;
  renderer->projection_texture_xz = 0u;
  renderer->projection_texture_yz = 0u;
  renderer->volume_texture_intensity = 0u;
  renderer->volume_texture_position = 0u;
  renderer->volume_program = 0u;
  renderer->volume_uniform_intensity = -1;
  renderer->volume_uniform_position = -1;
  renderer->volume_uniform_brightness = -1;
  renderer->volume_uniform_contrast = -1;
  renderer->volume_uniform_alpha_scale = -1;
  renderer->volume_uniform_palette = -1;
  renderer->volume_uniform_depth = -1;
  renderer->volume_uniform_view = -1;
  renderer->volume_uniform_aspect = -1;
  renderer->volume_uniform_bounds_min = -1;
  renderer->volume_uniform_bounds_max = -1;
  renderer->volume_uniform_geom_min = -1;
  renderer->volume_uniform_geom_max = -1;
  renderer->vertex_buffer = 0u;
  renderer->mode = HV_3D_RENDERER_MODE_NONE;
  renderer->vertex_count = 0u;
  renderer->volume_side = 0u;
}

static int hv_byte_cube_is_renderable(const HvByteCube3D *cube)
{
  return (
    (cube != 0) &&
    (cube->voxels != 0) &&
    (cube->side == HV_BYTE_CUBE_SIDE) &&
    (cube->total_voxels == HV_BYTE_CUBE_TOTAL_VOXELS)
  );
}
#endif

float hv_3d_byte_cube_clamp_unit(float value);

const char *hv_3d_byte_cube_palette_name(Hv3DByteCubePalette palette)
{
  switch (palette) {
    case HV_3D_BYTE_CUBE_PALETTE_RGB:
      return "rgb";
    case HV_3D_BYTE_CUBE_PALETTE_HEAT:
      return "heat";
    case HV_3D_BYTE_CUBE_PALETTE_ASCII:
      return "ascii";
    case HV_3D_BYTE_CUBE_PALETTE_MONO:
      return "mono";
    default:
      return "unknown";
  }
}

int hv_3d_byte_cube_parse_palette(const char *text, Hv3DByteCubePalette *palette_out)
{
  if ((text == 0) || (palette_out == 0)) {
    return 0;
  }
  if (strcmp(text, "rgb") == 0) {
    *palette_out = HV_3D_BYTE_CUBE_PALETTE_RGB;
    return 1;
  }
  if (strcmp(text, "heat") == 0) {
    *palette_out = HV_3D_BYTE_CUBE_PALETTE_HEAT;
    return 1;
  }
  if (strcmp(text, "ascii") == 0) {
    *palette_out = HV_3D_BYTE_CUBE_PALETTE_ASCII;
    return 1;
  }
  if (strcmp(text, "mono") == 0) {
    *palette_out = HV_3D_BYTE_CUBE_PALETTE_MONO;
    return 1;
  }
  return 0;
}

const char *hv_3d_byte_cube_blend_mode_name(Hv3DByteCubeBlendMode blend_mode)
{
  switch (blend_mode) {
    case HV_3D_BYTE_CUBE_BLEND_ACCUMULATE:
      return "accumulate";
    case HV_3D_BYTE_CUBE_BLEND_ALPHA:
      return "alpha";
    default:
      return "unknown";
  }
}

int hv_3d_byte_cube_parse_blend_mode(const char *text, Hv3DByteCubeBlendMode *blend_mode_out)
{
  if ((text == 0) || (blend_mode_out == 0)) {
    return 0;
  }
  if (strcmp(text, "accumulate") == 0) {
    *blend_mode_out = HV_3D_BYTE_CUBE_BLEND_ACCUMULATE;
    return 1;
  }
  if (strcmp(text, "alpha") == 0) {
    *blend_mode_out = HV_3D_BYTE_CUBE_BLEND_ALPHA;
    return 1;
  }
  return 0;
}

const char *hv_3d_byte_cube_projection_name(Hv3DByteCubeProjection projection)
{
  switch (projection) {
    case HV_3D_BYTE_CUBE_PROJECTION_FREE_3D:
      return "free-3d";
    case HV_3D_BYTE_CUBE_PROJECTION_XY:
      return "xy";
    case HV_3D_BYTE_CUBE_PROJECTION_XZ:
      return "xz";
    case HV_3D_BYTE_CUBE_PROJECTION_YZ:
      return "yz";
    default:
      return "unknown";
  }
}

int hv_3d_byte_cube_parse_projection(const char *text, Hv3DByteCubeProjection *projection_out)
{
  if ((text == 0) || (projection_out == 0)) {
    return 0;
  }
  if (strcmp(text, "free-3d") == 0) {
    *projection_out = HV_3D_BYTE_CUBE_PROJECTION_FREE_3D;
    return 1;
  }
  if (strcmp(text, "xy") == 0) {
    *projection_out = HV_3D_BYTE_CUBE_PROJECTION_XY;
    return 1;
  }
  if (strcmp(text, "xz") == 0) {
    *projection_out = HV_3D_BYTE_CUBE_PROJECTION_XZ;
    return 1;
  }
  if (strcmp(text, "yz") == 0) {
    *projection_out = HV_3D_BYTE_CUBE_PROJECTION_YZ;
    return 1;
  }
  return 0;
}

const char *hv_3d_byte_cube_interpolation_name(Hv3DByteCubeInterpolation interpolation)
{
  switch (interpolation) {
    case HV_3D_BYTE_CUBE_INTERPOLATION_LINEAR:
      return "linear";
    case HV_3D_BYTE_CUBE_INTERPOLATION_NEAREST:
      return "nearest";
    default:
      return "unknown";
  }
}

int hv_3d_byte_cube_parse_interpolation(const char *text, Hv3DByteCubeInterpolation *interpolation_out)
{
  if ((text == 0) || (interpolation_out == 0)) {
    return 0;
  }
  if (strcmp(text, "linear") == 0) {
    *interpolation_out = HV_3D_BYTE_CUBE_INTERPOLATION_LINEAR;
    return 1;
  }
  if (strcmp(text, "nearest") == 0) {
    *interpolation_out = HV_3D_BYTE_CUBE_INTERPOLATION_NEAREST;
    return 1;
  }
  return 0;
}

int hv_3d_byte_cube_projection_view(
  Hv3DByteCubeProjection projection,
  int *use_free_camera_out,
  float *pitch_degrees_out,
  float *yaw_degrees_out
)
{
  if ((use_free_camera_out == 0) || (pitch_degrees_out == 0) || (yaw_degrees_out == 0)) {
    return 0;
  }

  switch (projection) {
    case HV_3D_BYTE_CUBE_PROJECTION_FREE_3D:
      *use_free_camera_out = 1;
      *pitch_degrees_out = 0.0f;
      *yaw_degrees_out = 0.0f;
      return 1;
    case HV_3D_BYTE_CUBE_PROJECTION_XY:
      *use_free_camera_out = 0;
      *pitch_degrees_out = 0.0f;
      *yaw_degrees_out = 0.0f;
      return 1;
    case HV_3D_BYTE_CUBE_PROJECTION_XZ:
      *use_free_camera_out = 0;
      *pitch_degrees_out = 90.0f;
      *yaw_degrees_out = 0.0f;
      return 1;
    case HV_3D_BYTE_CUBE_PROJECTION_YZ:
      *use_free_camera_out = 0;
      *pitch_degrees_out = 0.0f;
      *yaw_degrees_out = 90.0f;
      return 1;
    default:
      return 0;
  }
}

int hv_3d_byte_cube_is_printable_trigram(float x, float y, float z)
{
  int printable_votes = 0;

  x = hv_3d_byte_cube_clamp_unit(x);
  y = hv_3d_byte_cube_clamp_unit(y);
  z = hv_3d_byte_cube_clamp_unit(z);

  if ((x >= (32.0f / 255.0f)) && (x <= (126.0f / 255.0f))) {
    ++printable_votes;
  }
  if ((y >= (32.0f / 255.0f)) && (y <= (126.0f / 255.0f))) {
    ++printable_votes;
  }
  if ((z >= (32.0f / 255.0f)) && (z <= (126.0f / 255.0f))) {
    ++printable_votes;
  }

  return (printable_votes >= 2) ? 1 : 0;
}

int hv_3d_byte_cube_palette_color(
  Hv3DByteCubePalette palette,
  float x,
  float y,
  float z,
  float density,
  float *r_out,
  float *g_out,
  float *b_out
)
{
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
  float d = hv_3d_byte_cube_clamp_unit(density);

  if ((r_out == 0) || (g_out == 0) || (b_out == 0)) {
    return 0;
  }

  x = hv_3d_byte_cube_clamp_unit(x);
  y = hv_3d_byte_cube_clamp_unit(y);
  z = hv_3d_byte_cube_clamp_unit(z);

  switch (palette) {
    case HV_3D_BYTE_CUBE_PALETTE_RGB:
      r = 0.22f + (0.78f * powf(x, 0.45f));
      g = 0.22f + (0.78f * powf(y, 0.45f));
      b = 0.22f + (0.78f * powf(z, 0.45f));
      break;
    case HV_3D_BYTE_CUBE_PALETTE_HEAT:
      r = hv_3d_byte_cube_clamp_unit(0.25f + (1.10f * d));
      g = hv_3d_byte_cube_clamp_unit((d - 0.18f) * 1.35f);
      b = hv_3d_byte_cube_clamp_unit((d - 0.70f) * 2.60f);
      break;
    case HV_3D_BYTE_CUBE_PALETTE_ASCII:
      if (hv_3d_byte_cube_is_printable_trigram(x, y, z)) {
        r = hv_3d_byte_cube_clamp_unit(0.65f + (0.35f * d));
        g = hv_3d_byte_cube_clamp_unit(0.45f + (0.45f * d));
        b = hv_3d_byte_cube_clamp_unit(0.10f + (0.15f * d));
      } else {
        r = hv_3d_byte_cube_clamp_unit(0.15f + (0.20f * d));
        g = hv_3d_byte_cube_clamp_unit(0.28f + (0.30f * d));
        b = hv_3d_byte_cube_clamp_unit(0.45f + (0.40f * d));
      }
      break;
    case HV_3D_BYTE_CUBE_PALETTE_MONO:
      r = 0.20f + (0.80f * d);
      g = r;
      b = r;
      break;
    default:
      return 0;
  }

  *r_out = hv_3d_byte_cube_clamp_unit(r);
  *g_out = hv_3d_byte_cube_clamp_unit(g);
  *b_out = hv_3d_byte_cube_clamp_unit(b);
  return 1;
}

float hv_3d_byte_cube_volume_alpha_scale(float zoom, Hv3DByteCubeBlendMode blend_mode)
{
  if (blend_mode != HV_3D_BYTE_CUBE_BLEND_ACCUMULATE) {
    return 1.0f;
  }
  if (zoom <= 1.0f) {
    return 1.0f;
  }
  {
    float scaled = 1.0f / zoom;
    if (scaled < 0.12f) {
      scaled = 0.12f;
    }
    return hv_3d_byte_cube_clamp_unit(scaled);
  }
}

unsigned int hv_3d_byte_cube_volume_layers(float zoom)
{
  const unsigned int base_layers = HV_BYTE_CUBE_SIDE * 4u;
  unsigned int scale = 1u;

  if (!isfinite((double)zoom) || (zoom <= 1.0f)) {
    return base_layers;
  }

  scale = (unsigned int)ceilf(zoom * zoom);
  if (scale < 1u) {
    scale = 1u;
  } else if (scale > 8u) {
    scale = 8u;
  }
  return base_layers * scale;
}

int hv_3d_renderer_center_square_viewport(
  uint32_t viewport_width,
  uint32_t viewport_height,
  int *x_out,
  int *y_out,
  uint32_t *side_out
)
{
  uint32_t side = 0u;

  if ((x_out == 0) || (y_out == 0) || (side_out == 0)) {
    return 0;
  }

  if (viewport_width == 0u) {
    viewport_width = 1u;
  }
  if (viewport_height == 0u) {
    viewport_height = 1u;
  }

  side = (viewport_width < viewport_height) ? viewport_width : viewport_height;
  *x_out = (int)((viewport_width - side) / 2u);
  *y_out = (int)((viewport_height - side) / 2u);
  *side_out = side;
  return 1;
}

void hv_3d_byte_cube_view_settings_init_defaults(Hv3DByteCubeViewSettings *settings)
{
  if (settings == 0) {
    return;
  }

  settings->brightness = 0.0f;
  settings->contrast = 1.0f;
  settings->palette = HV_3D_BYTE_CUBE_PALETTE_RGB;
  settings->blend_mode = HV_3D_BYTE_CUBE_BLEND_ACCUMULATE;
  settings->projection = HV_3D_BYTE_CUBE_PROJECTION_FREE_3D;
  settings->interpolation = HV_3D_BYTE_CUBE_INTERPOLATION_NEAREST;
  settings->position_interpolation = HV_3D_BYTE_CUBE_INTERPOLATION_NEAREST;
}

int hv_3d_byte_cube_view_settings_validate(
  const Hv3DByteCubeViewSettings *settings,
  char *err,
  size_t err_size
)
{
  if (settings == 0) {
    hv_set_error(err, err_size, "invalid byte-cube view settings");
    return 0;
  }
  if (!isfinite((double)settings->brightness) || !isfinite((double)settings->contrast)) {
    hv_set_error(err, err_size, "invalid byte-cube brightness/contrast");
    return 0;
  }
  if (settings->contrast <= 0.0f) {
    hv_set_error(err, err_size, "byte-cube contrast must be positive");
    return 0;
  }
  if (
    (settings->palette < HV_3D_BYTE_CUBE_PALETTE_RGB) ||
    (settings->palette > HV_3D_BYTE_CUBE_PALETTE_MONO)
  ) {
    hv_set_error(err, err_size, "invalid byte-cube palette");
    return 0;
  }
  if (
    (settings->blend_mode < HV_3D_BYTE_CUBE_BLEND_ACCUMULATE) ||
    (settings->blend_mode > HV_3D_BYTE_CUBE_BLEND_ALPHA)
  ) {
    hv_set_error(err, err_size, "invalid byte-cube blend mode");
    return 0;
  }
  if (
    (settings->projection < HV_3D_BYTE_CUBE_PROJECTION_FREE_3D) ||
    (settings->projection > HV_3D_BYTE_CUBE_PROJECTION_YZ)
  ) {
    hv_set_error(err, err_size, "invalid byte-cube projection");
    return 0;
  }
  if (
    (settings->interpolation < HV_3D_BYTE_CUBE_INTERPOLATION_LINEAR) ||
    (settings->interpolation > HV_3D_BYTE_CUBE_INTERPOLATION_NEAREST)
  ) {
    hv_set_error(err, err_size, "invalid byte-cube interpolation");
    return 0;
  }
  if (
    (settings->position_interpolation < HV_3D_BYTE_CUBE_INTERPOLATION_LINEAR) ||
    (settings->position_interpolation > HV_3D_BYTE_CUBE_INTERPOLATION_NEAREST)
  ) {
    hv_set_error(err, err_size, "invalid byte-cube position interpolation");
    return 0;
  }

  return 1;
}

float hv_3d_byte_cube_clamp_unit(float value)
{
  if (value < 0.0f) {
    return 0.0f;
  }
  if (value > 1.0f) {
    return 1.0f;
  }
  return value;
}

float hv_3d_renderer_byte_cube_local_coord(float texcoord)
{
  return (hv_3d_byte_cube_clamp_unit(texcoord) * 2.0f) - 1.0f;
}

float hv_3d_byte_cube_density_normalize(uint32_t count, uint32_t max_density)
{
  double max_log_density = 0.0;
  if ((count == 0u) || (max_density == 0u)) {
    return 0.0f;
  }

  max_log_density = log((double)max_density + 1.0);
  if (max_log_density <= 0.0) {
    return 0.0f;
  }

  return hv_3d_byte_cube_clamp_unit((float)(log((double)count + 1.0) / max_log_density));
}

float hv_3d_byte_cube_apply_brightness(float value, float brightness)
{
  return hv_3d_byte_cube_clamp_unit(value + brightness);
}

float hv_3d_byte_cube_apply_contrast(float value, float contrast)
{
  float shifted = 0.0f;

  if (!isfinite((double)contrast) || (contrast <= 0.0f)) {
    return hv_3d_byte_cube_clamp_unit(value);
  }

  shifted = ((value - 0.5f) * contrast) + 0.5f;
  return hv_3d_byte_cube_clamp_unit(shifted);
}

float hv_3d_byte_cube_density_transfer(
  uint32_t count,
  uint32_t max_density,
  float brightness,
  float contrast
)
{
  float normalized = hv_3d_byte_cube_density_normalize(count, max_density);

  normalized = hv_3d_byte_cube_apply_brightness(normalized, brightness);
  normalized = hv_3d_byte_cube_apply_contrast(normalized, contrast);
  return hv_3d_byte_cube_clamp_unit(normalized);
}

uint8_t hv_3d_renderer_byte_cube_alpha(
  uint32_t count,
  uint32_t max_density,
  const Hv3DByteCubeViewSettings *settings
)
{
  float transferred = 0.0f;

  if (settings == 0) {
    return 0u;
  }
  if (count == 0u) {
    return 0u;
  }

  transferred = hv_3d_byte_cube_density_transfer(count, max_density, settings->brightness, settings->contrast);
  return (uint8_t)((transferred * 255.0f) + 0.5f);
}

int hv_3d_renderer_init(
  Hv3DRenderer *renderer,
  const HvPointCloud3D *cloud,
  char *err,
  size_t err_size
)
{
#if defined(HV_3D_VIEWER_AVAILABLE)
  size_t bytes = 0u;

  if ((renderer == 0) || (cloud == 0)) {
    hv_set_error(err, err_size, "invalid arguments for 3D renderer initialization");
    return 0;
  }

  hv_3d_renderer_reset(renderer);
  renderer->mode = HV_3D_RENDERER_MODE_POINTS;
  renderer->vertex_count = cloud->count;

  if (
    !hv_3d_renderer_load_buffer_functions(err, err_size)
  ) {
    return 0;
  }

  if (cloud->count == 0u) {
    return 1;
  }

  if (cloud->count > (SIZE_MAX / sizeof(*cloud->points))) {
    hv_set_error(err, err_size, "3D renderer buffer size overflow");
    return 0;
  }
  bytes = cloud->count * sizeof(*cloud->points);

  hv_glGenBuffers_ptr(1, &renderer->vertex_buffer);
  if (renderer->vertex_buffer == 0u) {
    hv_set_error(err, err_size, "failed to allocate OpenGL vertex buffer");
    return 0;
  }

  hv_glBindBuffer_ptr(GL_ARRAY_BUFFER, renderer->vertex_buffer);
  hv_glBufferData_ptr(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, cloud->points, GL_STATIC_DRAW);
  hv_glBindBuffer_ptr(GL_ARRAY_BUFFER, 0u);
  return 1;
#else
  if ((renderer == 0) || (cloud == 0)) {
    hv_set_error(err, err_size, "invalid arguments for 3D renderer initialization");
    return 0;
  }

  hv_set_error(err, err_size, "3D renderer is unavailable in this build");
  return 0;
#endif
}

int hv_3d_renderer_init_byte_cube(
  Hv3DRenderer *renderer,
  const HvByteCube3D *cube,
  const Hv3DByteCubeViewSettings *settings,
  char *err,
  size_t err_size
)
{
#if defined(HV_3D_VIEWER_AVAILABLE)
  float *volume_intensity = 0;
  uint8_t *volume_position = 0;
  uint64_t *projection_xy = 0;
  uint64_t *projection_xz = 0;
  uint64_t *projection_yz = 0;
  uint8_t *projection_rgba = 0;
  size_t volume_size = (size_t)HV_BYTE_CUBE_TOTAL_VOXELS;
  size_t projection_size = (size_t)HV_BYTE_CUBE_SIDE * (size_t)HV_BYTE_CUBE_SIDE;
  size_t idx = 0u;
  size_t x = 0u;
  size_t y = 0u;
  size_t z = 0u;
  uint64_t max_xy = 0u;
  uint64_t max_xz = 0u;
  uint64_t max_yz = 0u;

  if ((renderer == 0) || (cube == 0) || (settings == 0)) {
    hv_set_error(err, err_size, "invalid arguments for 3D byte-cube renderer initialization");
    return 0;
  }
  if (!hv_3d_byte_cube_view_settings_validate(settings, err, err_size)) {
    return 0;
  }
  if (!hv_byte_cube_is_renderable(cube)) {
    hv_set_error(err, err_size, "invalid byte-cube state for 3D renderer");
    return 0;
  }

  hv_3d_renderer_reset(renderer);
  renderer->mode = HV_3D_RENDERER_MODE_BYTE_CUBE;
  renderer->volume_side = cube->side;
  renderer->vertex_count = 0u;
  if (cube->occupied_voxels > 0u) {
    float half_texel = 0.5f / (float)cube->side;

    renderer->tex_min_x = (((float)cube->occupied_min_x + 0.5f) / (float)cube->side);
    renderer->tex_min_y = (((float)cube->occupied_min_y + 0.5f) / (float)cube->side);
    renderer->tex_min_z = (((float)cube->occupied_min_z + 0.5f) / (float)cube->side);
    renderer->tex_max_x = (((float)cube->occupied_max_x + 0.5f) / (float)cube->side);
    renderer->tex_max_y = (((float)cube->occupied_max_y + 0.5f) / (float)cube->side);
    renderer->tex_max_z = (((float)cube->occupied_max_z + 0.5f) / (float)cube->side);

    if (renderer->tex_min_x == renderer->tex_max_x) {
      renderer->tex_min_x -= half_texel;
      renderer->tex_max_x += half_texel;
    }
    if (renderer->tex_min_y == renderer->tex_max_y) {
      renderer->tex_min_y -= half_texel;
      renderer->tex_max_y += half_texel;
    }
    if (renderer->tex_min_z == renderer->tex_max_z) {
      renderer->tex_min_z -= half_texel;
      renderer->tex_max_z += half_texel;
    }
    if (renderer->tex_min_x < 0.0f) {
      renderer->tex_min_x = 0.0f;
    }
    if (renderer->tex_min_y < 0.0f) {
      renderer->tex_min_y = 0.0f;
    }
    if (renderer->tex_min_z < 0.0f) {
      renderer->tex_min_z = 0.0f;
    }
    if (renderer->tex_max_x > 1.0f) {
      renderer->tex_max_x = 1.0f;
    }
    if (renderer->tex_max_y > 1.0f) {
      renderer->tex_max_y = 1.0f;
    }
    if (renderer->tex_max_z > 1.0f) {
      renderer->tex_max_z = 1.0f;
    }
  }
  renderer->projection_zoom = 1.0f;

  if (!hv_3d_renderer_load_buffer_functions(err, err_size)) {
    return 0;
  }
  if (volume_size > (SIZE_MAX / sizeof(*volume_intensity))) {
    hv_set_error(err, err_size, "byte-cube intensity-volume size exceeds host size_t");
    return 0;
  }
  if (projection_size > (SIZE_MAX / sizeof(*projection_xy))) {
    hv_set_error(err, err_size, "byte-cube projection buffer size exceeds host size_t");
    return 0;
  }

  volume_intensity = (float *)calloc(volume_size, sizeof(*volume_intensity));
  volume_position = (uint8_t *)malloc(volume_size * 3u);
  if ((volume_intensity == 0) || (volume_position == 0)) {
    free(volume_intensity);
    free(volume_position);
    hv_set_error(err, err_size, "failed to allocate byte-cube volume textures");
    return 0;
  }
  projection_xy = (uint64_t *)calloc(projection_size, sizeof(*projection_xy));
  projection_xz = (uint64_t *)calloc(projection_size, sizeof(*projection_xz));
  projection_yz = (uint64_t *)calloc(projection_size, sizeof(*projection_yz));
  projection_rgba = (uint8_t *)malloc(projection_size * 4u);
  if ((projection_xy == 0) || (projection_xz == 0) || (projection_yz == 0) || (projection_rgba == 0)) {
    free(volume_intensity);
    free(volume_position);
    free(projection_xy);
    free(projection_xz);
    free(projection_yz);
    free(projection_rgba);
    hv_set_error(err, err_size, "failed to allocate byte-cube projection buffers");
    return 0;
  }

  for (x = 0u; x < (size_t)HV_BYTE_CUBE_SIDE; ++x) {
    for (y = 0u; y < (size_t)HV_BYTE_CUBE_SIDE; ++y) {
      for (z = 0u; z < (size_t)HV_BYTE_CUBE_SIDE; ++z) {
        uint32_t count = cube->voxels[idx];

        volume_position[(idx * 3u) + 0u] = (uint8_t)x;
        volume_position[(idx * 3u) + 1u] = (uint8_t)y;
        volume_position[(idx * 3u) + 2u] = (uint8_t)z;

        if (count > 0u) {
          float density = hv_3d_byte_cube_density_normalize(count, cube->max_density);

          volume_intensity[idx] = density;
          projection_xy[(x * (size_t)HV_BYTE_CUBE_SIDE) + y] += (uint64_t)count;
          projection_xz[(x * (size_t)HV_BYTE_CUBE_SIDE) + z] += (uint64_t)count;
          projection_yz[(y * (size_t)HV_BYTE_CUBE_SIDE) + z] += (uint64_t)count;

          if (projection_xy[(x * (size_t)HV_BYTE_CUBE_SIDE) + y] > max_xy) {
            max_xy = projection_xy[(x * (size_t)HV_BYTE_CUBE_SIDE) + y];
          }
          if (projection_xz[(x * (size_t)HV_BYTE_CUBE_SIDE) + z] > max_xz) {
            max_xz = projection_xz[(x * (size_t)HV_BYTE_CUBE_SIDE) + z];
          }
          if (projection_yz[(y * (size_t)HV_BYTE_CUBE_SIDE) + z] > max_yz) {
            max_yz = projection_yz[(y * (size_t)HV_BYTE_CUBE_SIDE) + z];
          }
        }
        ++idx;
      }
    }
  }
  if (!hv_3d_renderer_link_volume_program(renderer, err, err_size)) {
    free(volume_intensity);
    free(volume_position);
    free(projection_xy);
    free(projection_xz);
    free(projection_yz);
    free(projection_rgba);
    hv_3d_renderer_reset(renderer);
    return 0;
  }
  if (!hv_3d_renderer_upload_byte_cube_volume_texture_float(
        &renderer->volume_texture_intensity,
        volume_intensity,
        GL_LUMINANCE,
        GL_LUMINANCE,
        settings,
        err,
        err_size
      )) {
    free(volume_intensity);
    free(volume_position);
    free(projection_xy);
    free(projection_xz);
    free(projection_yz);
    free(projection_rgba);
    hv_3d_renderer_shutdown(renderer);
    return 0;
  }
  if (!hv_3d_renderer_upload_byte_cube_volume_texture_u8(
        &renderer->volume_texture_position,
        volume_position,
        settings,
        err,
        err_size
      )) {
    free(volume_intensity);
    free(volume_position);
    free(projection_xy);
    free(projection_xz);
    free(projection_yz);
    free(projection_rgba);
    hv_3d_renderer_shutdown(renderer);
    return 0;
  }
  free(volume_intensity);
  free(volume_position);

  for (x = 0u; x < (size_t)HV_BYTE_CUBE_SIDE; ++x) {
    for (y = 0u; y < (size_t)HV_BYTE_CUBE_SIDE; ++y) {
      size_t proj_index = (x * (size_t)HV_BYTE_CUBE_SIDE) + y;
      float density = hv_3d_byte_cube_density_transfer(
        (uint32_t)((projection_xy[proj_index] > (uint64_t)UINT32_MAX) ? UINT32_MAX : projection_xy[proj_index]),
        (uint32_t)((max_xy > (uint64_t)UINT32_MAX) ? UINT32_MAX : max_xy),
        settings->brightness,
        settings->contrast
      );
      float r = 0.0f;
      float g = 0.0f;
      float b = 0.0f;

      (void)hv_3d_byte_cube_palette_color(settings->palette, (float)x / 255.0f, (float)y / 255.0f, 0.5f, density, &r, &g, &b);
      projection_rgba[(proj_index * 4u) + 0u] = (uint8_t)(r * 255.0f + 0.5f);
      projection_rgba[(proj_index * 4u) + 1u] = (uint8_t)(g * 255.0f + 0.5f);
      projection_rgba[(proj_index * 4u) + 2u] = (uint8_t)(b * 255.0f + 0.5f);
      projection_rgba[(proj_index * 4u) + 3u] = (uint8_t)(density * 255.0f + 0.5f);
    }
  }
  if (!hv_3d_renderer_upload_projection_texture(&renderer->projection_texture_xy, projection_rgba, settings, err, err_size)) {
    free(projection_xy);
    free(projection_xz);
    free(projection_yz);
    free(projection_rgba);
    hv_3d_renderer_shutdown(renderer);
    return 0;
  }

  for (x = 0u; x < (size_t)HV_BYTE_CUBE_SIDE; ++x) {
    for (z = 0u; z < (size_t)HV_BYTE_CUBE_SIDE; ++z) {
      size_t proj_index = (x * (size_t)HV_BYTE_CUBE_SIDE) + z;
      float density = hv_3d_byte_cube_density_transfer(
        (uint32_t)((projection_xz[proj_index] > (uint64_t)UINT32_MAX) ? UINT32_MAX : projection_xz[proj_index]),
        (uint32_t)((max_xz > (uint64_t)UINT32_MAX) ? UINT32_MAX : max_xz),
        settings->brightness,
        settings->contrast
      );
      float r = 0.0f;
      float g = 0.0f;
      float b = 0.0f;

      (void)hv_3d_byte_cube_palette_color(settings->palette, (float)x / 255.0f, 0.5f, (float)z / 255.0f, density, &r, &g, &b);
      projection_rgba[(proj_index * 4u) + 0u] = (uint8_t)(r * 255.0f + 0.5f);
      projection_rgba[(proj_index * 4u) + 1u] = (uint8_t)(g * 255.0f + 0.5f);
      projection_rgba[(proj_index * 4u) + 2u] = (uint8_t)(b * 255.0f + 0.5f);
      projection_rgba[(proj_index * 4u) + 3u] = (uint8_t)(density * 255.0f + 0.5f);
    }
  }
  if (!hv_3d_renderer_upload_projection_texture(&renderer->projection_texture_xz, projection_rgba, settings, err, err_size)) {
    free(projection_xy);
    free(projection_xz);
    free(projection_yz);
    free(projection_rgba);
    hv_3d_renderer_shutdown(renderer);
    return 0;
  }

  for (y = 0u; y < (size_t)HV_BYTE_CUBE_SIDE; ++y) {
    for (z = 0u; z < (size_t)HV_BYTE_CUBE_SIDE; ++z) {
      size_t proj_index = (y * (size_t)HV_BYTE_CUBE_SIDE) + z;
      float density = hv_3d_byte_cube_density_transfer(
        (uint32_t)((projection_yz[proj_index] > (uint64_t)UINT32_MAX) ? UINT32_MAX : projection_yz[proj_index]),
        (uint32_t)((max_yz > (uint64_t)UINT32_MAX) ? UINT32_MAX : max_yz),
        settings->brightness,
        settings->contrast
      );
      float r = 0.0f;
      float g = 0.0f;
      float b = 0.0f;

      (void)hv_3d_byte_cube_palette_color(settings->palette, 0.5f, (float)y / 255.0f, (float)z / 255.0f, density, &r, &g, &b);
      projection_rgba[(proj_index * 4u) + 0u] = (uint8_t)(r * 255.0f + 0.5f);
      projection_rgba[(proj_index * 4u) + 1u] = (uint8_t)(g * 255.0f + 0.5f);
      projection_rgba[(proj_index * 4u) + 2u] = (uint8_t)(b * 255.0f + 0.5f);
      projection_rgba[(proj_index * 4u) + 3u] = (uint8_t)(density * 255.0f + 0.5f);
    }
  }
  if (!hv_3d_renderer_upload_projection_texture(&renderer->projection_texture_yz, projection_rgba, settings, err, err_size)) {
    free(projection_xy);
    free(projection_xz);
    free(projection_yz);
    free(projection_rgba);
    hv_3d_renderer_shutdown(renderer);
    return 0;
  }

  free(projection_xy);
  free(projection_xz);
  free(projection_yz);
  free(projection_rgba);
  return 1;
#else
  if ((renderer == 0) || (cube == 0) || (settings == 0)) {
    hv_set_error(err, err_size, "invalid arguments for 3D byte-cube renderer initialization");
    return 0;
  }
  if (!hv_3d_byte_cube_view_settings_validate(settings, err, err_size)) {
    return 0;
  }
  if ((cube->voxels == 0) ||
      (cube->side != HV_BYTE_CUBE_SIDE) ||
      (cube->total_voxels != HV_BYTE_CUBE_TOTAL_VOXELS)) {
    hv_set_error(err, err_size, "invalid byte-cube state for 3D renderer");
    return 0;
  }

  hv_set_error(err, err_size, "3D renderer is unavailable in this build");
  return 0;
#endif
}

int hv_3d_renderer_validate_point_size(float point_size, char *err, size_t err_size)
{
  if ((point_size < HV_3D_POINT_SIZE_MIN) || (point_size > HV_3D_POINT_SIZE_MAX)) {
    hv_set_error(
      err,
      err_size,
      "invalid 3D point size %.3f (supported range: %.1f..%.1f)",
      (double)point_size,
      (double)HV_3D_POINT_SIZE_MIN,
      (double)HV_3D_POINT_SIZE_MAX
    );
    return 0;
  }

  return 1;
}

void hv_3d_renderer_shutdown(Hv3DRenderer *renderer)
{
#if defined(HV_3D_VIEWER_AVAILABLE)
  if (renderer == 0) {
    return;
  }
  if ((renderer->vertex_buffer != 0u) && (hv_glDeleteBuffers_ptr != 0)) {
    hv_glDeleteBuffers_ptr(1, &renderer->vertex_buffer);
  }
  if (renderer->projection_texture_xy != 0u) {
    glDeleteTextures(1, &renderer->projection_texture_xy);
  }
  if (renderer->projection_texture_xz != 0u) {
    glDeleteTextures(1, &renderer->projection_texture_xz);
  }
  if (renderer->projection_texture_yz != 0u) {
    glDeleteTextures(1, &renderer->projection_texture_yz);
  }
  if (renderer->volume_texture_intensity != 0u) {
    glDeleteTextures(1, &renderer->volume_texture_intensity);
  }
  if (renderer->volume_texture_position != 0u) {
    glDeleteTextures(1, &renderer->volume_texture_position);
  }
  if (renderer->volume_program != 0u) {
    glDeleteProgram(renderer->volume_program);
  }
  hv_3d_renderer_reset(renderer);
#else
  (void)renderer;
#endif
}

int hv_3d_renderer_draw(
  const Hv3DRenderer *renderer,
  const Hv3DCamera *camera,
  uint32_t viewport_width,
  uint32_t viewport_height,
  float point_size,
  char *err,
  size_t err_size
)
{
#if defined(HV_3D_VIEWER_AVAILABLE)
  double aspect = 1.0;
  double near_plane = 1.0;
  double far_plane = 10.0;
  double top = 0.75 * near_plane;
  double right = 0.0;

  if ((renderer == 0) || (camera == 0)) {
    hv_set_error(err, err_size, "invalid arguments for 3D renderer draw");
    return 0;
  }
  if (renderer->mode != HV_3D_RENDERER_MODE_POINTS) {
    hv_set_error(err, err_size, "3D renderer is not initialized for point-cloud drawing");
    return 0;
  }
  if (!hv_3d_renderer_validate_point_size(point_size, err, err_size)) {
    return 0;
  }

  glViewport(0, 0, (GLsizei)viewport_width, (GLsizei)viewport_height);
  glClearColor(0.10f, 0.10f, 0.11f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glPointSize(point_size);

  if (viewport_height == 0u) {
    viewport_height = 1u;
  }
  aspect = (double)viewport_width / (double)viewport_height;
  right = top * aspect;

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glFrustum(-right, right, -top, top, near_plane, far_plane);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glTranslatef(0.0f, 0.0f, -camera->distance);
  glRotatef(camera->pitch_degrees, 1.0f, 0.0f, 0.0f);
  glRotatef(camera->yaw_degrees, 0.0f, 1.0f, 0.0f);
  glTranslatef(-camera->target_x, -camera->target_y, -camera->target_z);

  if ((renderer->vertex_count > 0u) && (renderer->vertex_buffer != 0u)) {
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    hv_glBindBuffer_ptr(GL_ARRAY_BUFFER, renderer->vertex_buffer);
    glVertexPointer(3, GL_FLOAT, (GLsizei)sizeof(HvPoint3D), (const void *)offsetof(HvPoint3D, x));
    glColorPointer(3, GL_UNSIGNED_BYTE, (GLsizei)sizeof(HvPoint3D), (const void *)offsetof(HvPoint3D, r));
    glDrawArrays(GL_POINTS, 0, (GLsizei)renderer->vertex_count);
    hv_glBindBuffer_ptr(GL_ARRAY_BUFFER, 0u);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
  }

  return 1;
#else
  (void)viewport_width;
  (void)viewport_height;
  if (!hv_3d_renderer_validate_point_size(point_size, err, err_size)) {
    return 0;
  }
  if ((renderer == 0) || (camera == 0)) {
    hv_set_error(err, err_size, "invalid arguments for 3D renderer draw");
    return 0;
  }

  hv_set_error(err, err_size, "3D renderer is unavailable in this build");
  return 0;
#endif
}

int hv_3d_renderer_draw_byte_cube(
  const Hv3DRenderer *renderer,
  const Hv3DCamera *camera,
  const Hv3DByteCubeViewSettings *settings,
  uint32_t viewport_width,
  uint32_t viewport_height,
  char *err,
  size_t err_size
)
{
#if defined(HV_3D_VIEWER_AVAILABLE)
  double aspect = 1.0;
  int use_free_camera = 1;
  float fixed_pitch = 0.0f;
  float fixed_yaw = 0.0f;
  unsigned int projection_texture = 0u;
  float u0 = 0.0f;
  float v0 = 0.0f;
  float u1 = 1.0f;
  float v1 = 1.0f;
  float content_width = 1.0f;
  float content_height = 1.0f;
  float quad_half_width = 1.0f;
  float quad_half_height = 1.0f;
  float zoom = 1.0f;
  float view_matrix[16];
  unsigned int layers = HV_BYTE_CUBE_SIDE * 4u;
  unsigned int i = 0u;

  if ((renderer == 0) || (camera == 0) || (settings == 0)) {
    hv_set_error(err, err_size, "invalid arguments for 3D byte-cube renderer draw");
    return 0;
  }
  if (!hv_3d_byte_cube_view_settings_validate(settings, err, err_size)) {
    return 0;
  }
  if (renderer->mode != HV_3D_RENDERER_MODE_BYTE_CUBE) {
    hv_set_error(err, err_size, "3D renderer is not initialized for byte-cube drawing");
    return 0;
  }
  if (!hv_3d_byte_cube_projection_view(settings->projection, &use_free_camera, &fixed_pitch, &fixed_yaw)) {
    hv_set_error(err, err_size, "invalid byte-cube projection");
    return 0;
  }

  glClearColor(0.10f, 0.10f, 0.11f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (viewport_height == 0u) {
    viewport_height = 1u;
  }
  aspect = (double)viewport_width / (double)viewport_height;
  if (use_free_camera) {
    zoom = hv_3d_renderer_camera_zoom(camera);
    layers = hv_3d_byte_cube_volume_layers(zoom);
    hv_3d_renderer_build_byte_cube_view_matrix(camera, zoom, view_matrix);
    if ((renderer->volume_program == 0u) ||
        (renderer->volume_texture_intensity == 0u) ||
        (renderer->volume_texture_position == 0u)) {
      hv_set_error(err, err_size, "byte-cube volume renderer is unavailable");
      return 0;
    }
    hv_3d_renderer_set_byte_cube_texture_filters(renderer, settings);
    glViewport(0, 0, (GLsizei)viewport_width, (GLsizei)viewport_height);
    glEnable(GL_BLEND);
    if (settings->blend_mode == HV_3D_BYTE_CUBE_BLEND_ALPHA) {
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
      glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    }
    glDisable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glUseProgram(renderer->volume_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, renderer->volume_texture_intensity);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, renderer->volume_texture_position);
    glUniform1i(renderer->volume_uniform_intensity, 0);
    glUniform1i(renderer->volume_uniform_position, 1);
    glUniform1f(renderer->volume_uniform_brightness, settings->brightness);
    glUniform1f(renderer->volume_uniform_contrast, settings->contrast);
    glUniform1f(renderer->volume_uniform_alpha_scale, hv_3d_byte_cube_volume_alpha_scale(zoom, settings->blend_mode));
    glUniform1i(renderer->volume_uniform_palette, (GLint)settings->palette);
    glUniformMatrix4fv(renderer->volume_uniform_view, 1, GL_FALSE, view_matrix);
    glUniform1f(renderer->volume_uniform_aspect, (float)aspect);
    glUniform3f(
      renderer->volume_uniform_bounds_min,
      renderer->tex_min_x,
      renderer->tex_min_y,
      renderer->tex_min_z
    );
    glUniform3f(
      renderer->volume_uniform_bounds_max,
      renderer->tex_max_x,
      renderer->tex_max_y,
      renderer->tex_max_z
    );

    for (i = 0u; i <= layers; ++i) {
      glUniform1f(renderer->volume_uniform_depth, (float)i / (float)layers);
      glBegin(GL_QUADS);
      glVertex2f(-1.0f, -1.0f);
      glVertex2f(1.0f, -1.0f);
      glVertex2f(1.0f, 1.0f);
      glVertex2f(-1.0f, 1.0f);
      glEnd();
    }
    glUseProgram(0u);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, 0u);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, 0u);
    glDisable(GL_BLEND);
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glLoadIdentity();
    glViewport(0, 0, (GLsizei)viewport_width, (GLsizei)viewport_height);
    glOrtho(-aspect, aspect, -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glScalef(zoom, zoom, zoom);
    glRotatef(camera->pitch_degrees, 1.0f, 0.0f, 0.0f);
    glRotatef(camera->yaw_degrees, 0.0f, 1.0f, 0.0f);
    glTranslatef(-camera->target_x, -camera->target_y, -camera->target_z);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    hv_3d_renderer_draw_byte_cube_outline(renderer, camera);
    glDisable(GL_BLEND);
    return 1;
  }

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(-aspect, aspect, -1.0, 1.0, -1.0, 1.0);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  switch (settings->projection) {
    case HV_3D_BYTE_CUBE_PROJECTION_XY:
      projection_texture = renderer->projection_texture_xy;
      break;
    case HV_3D_BYTE_CUBE_PROJECTION_XZ:
      projection_texture = renderer->projection_texture_xz;
      break;
    case HV_3D_BYTE_CUBE_PROJECTION_YZ:
      projection_texture = renderer->projection_texture_yz;
      break;
    default:
      hv_set_error(err, err_size, "invalid byte-cube projection");
      return 0;
  }
  if (projection_texture == 0u) {
    hv_set_error(err, err_size, "byte-cube projection texture is unavailable");
    return 0;
  }

  hv_3d_renderer_projection_texcoords(renderer, settings->projection, &u0, &v0, &u1, &v1);
  content_width = u1 - u0;
  content_height = v1 - v0;
  if (content_width <= 0.0f) {
    content_width = 1.0f / (float)HV_BYTE_CUBE_SIDE;
  }
  if (content_height <= 0.0f) {
    content_height = 1.0f / (float)HV_BYTE_CUBE_SIDE;
  }
  zoom = hv_3d_renderer_camera_zoom(camera);
  if ((content_width / content_height) >= (float)aspect) {
    quad_half_width = (float)aspect * zoom;
    quad_half_height = ((float)aspect / (content_width / content_height)) * zoom;
  } else {
    quad_half_height = 1.0f * zoom;
    quad_half_width = (content_width / content_height) * zoom;
  }

  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, projection_texture);
  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
  glBegin(GL_QUADS);
  glTexCoord2f(u0, v0);
  glVertex2f(-quad_half_width, -quad_half_height);
  glTexCoord2f(u1, v0);
  glVertex2f(quad_half_width, -quad_half_height);
  glTexCoord2f(u1, v1);
  glVertex2f(quad_half_width, quad_half_height);
  glTexCoord2f(u0, v1);
  glVertex2f(-quad_half_width, quad_half_height);
  glEnd();
  glBindTexture(GL_TEXTURE_2D, 0u);
  glDisable(GL_TEXTURE_2D);
  return 1;
#else
  (void)viewport_width;
  (void)viewport_height;
  if ((renderer == 0) || (camera == 0) || (settings == 0)) {
    hv_set_error(err, err_size, "invalid arguments for 3D byte-cube renderer draw");
    return 0;
  }
  if (!hv_3d_byte_cube_view_settings_validate(settings, err, err_size)) {
    return 0;
  }

  hv_set_error(err, err_size, "3D renderer is unavailable in this build");
  return 0;
#endif
}

int hv_3d_renderer_write_point_cloud_summary(
  FILE *stream,
  const HvPointCloud3D *cloud,
  const Hv3DCamera *camera,
  char *err,
  size_t err_size
)
{
  (void)camera;

  if ((stream == 0) || (cloud == 0)) {
    hv_set_error(err, err_size, "invalid arguments for 3D renderer summary");
    return 0;
  }

  if (fprintf(
        stream,
        "Loaded point cloud: points=%zu order=%u side=%u capacity=%" PRIu64 "\n",
        cloud->count,
        cloud->order,
        cloud->side,
        cloud->capacity
      ) < 0) {
    hv_set_error(err, err_size, "failed to write 3D point cloud summary: %s", strerror(errno));
    return 0;
  }

  if (cloud->count > 0u) {
    const HvPoint3D *first = &cloud->points[0];
    const HvPoint3D *last = &cloud->points[cloud->count - 1u];

    if (
      (fprintf(
         stream,
         "First point: %.6f %.6f %.6f rgb=%u,%u,%u\n",
         (double)first->x,
         (double)first->y,
         (double)first->z,
         first->r,
         first->g,
         first->b
       ) < 0) ||
      (fprintf(
         stream,
         "Last point: %.6f %.6f %.6f rgb=%u,%u,%u\n",
         (double)last->x,
         (double)last->y,
         (double)last->z,
         last->r,
         last->g,
         last->b
       ) < 0)
    ) {
      hv_set_error(err, err_size, "failed to write 3D point cloud detail: %s", strerror(errno));
      return 0;
    }
  }

  return 1;
}
