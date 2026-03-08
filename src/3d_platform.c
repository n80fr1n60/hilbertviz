#include "3d_platform.h"

#include "3d_renderer.h"

#if defined(HV_3D_VIEWER_AVAILABLE)
#include <SDL2/SDL.h>
#endif

#include <errno.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define HV_PRINTF_LIKE(fmt_idx, first_arg_idx) __attribute__((format(printf, fmt_idx, first_arg_idx)))
#else
#define HV_PRINTF_LIKE(fmt_idx, first_arg_idx)
#endif

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

int hv_3d_platform_viewer_requested(void)
{
#if defined(HV_3D_VIEWER_REQUESTED)
  return 1;
#else
  return 0;
#endif
}

int hv_3d_platform_viewer_available(void)
{
#if defined(HV_3D_VIEWER_AVAILABLE)
  return 1;
#else
  return 0;
#endif
}

const char *hv_3d_platform_viewer_support_text(void)
{
  if (hv_3d_platform_viewer_available()) {
    return "available (SDL2/OpenGL detected; static render path enabled)";
  }
  if (hv_3d_platform_viewer_requested()) {
    return "unavailable (SDL2/OpenGL development packages not found)";
  }
  return "disabled at build time";
}

#if defined(HV_3D_VIEWER_AVAILABLE)
typedef struct Hv3DViewerState {
  int dragging_left;
  int dragging_right;
  int byte_cube_overview_active;
  int last_mouse_x;
  int last_mouse_y;
  uint32_t viewport_width;
  uint32_t viewport_height;
  int redraw_needed;
} Hv3DViewerState;

enum {
  HV_3D_PLATFORM_SCENE_CLOUD = 1,
  HV_3D_PLATFORM_SCENE_BYTE_CUBE = 2
};

static int hv_parse_truthy_env(const char *name)
{
  const char *value = getenv(name);

  if (value == 0) {
    return 0;
  }

  return
    (strcmp(value, "1") == 0) ||
    (strcmp(value, "true") == 0) ||
    (strcmp(value, "TRUE") == 0) ||
    (strcmp(value, "yes") == 0) ||
    (strcmp(value, "YES") == 0);
}

static int hv_parse_u32_env(const char *name, uint32_t *out, char *err, size_t err_size)
{
  const char *value = getenv(name);
  unsigned long parsed = 0ul;
  char *end = 0;

  if (out == 0) {
    hv_set_error(err, err_size, "invalid render-frame environment arguments");
    return 0;
  }

  *out = 0u;
  if ((value == 0) || (*value == '\0')) {
    return 1;
  }

  errno = 0;
  parsed = strtoul(value, &end, 10);
  if ((errno != 0) || (end == value) || (*end != '\0') || (parsed > (unsigned long)UINT32_MAX)) {
    hv_set_error(err, err_size, "invalid %s='%s' (expected unsigned decimal frames)", name, value);
    return 0;
  }

  *out = (uint32_t)parsed;
  return 1;
}

static void hv_3d_cycle_palette(Hv3DByteCubeViewSettings *settings)
{
  if (settings == 0) {
    return;
  }

  switch (settings->palette) {
    case HV_3D_BYTE_CUBE_PALETTE_RGB:
      settings->palette = HV_3D_BYTE_CUBE_PALETTE_HEAT;
      break;
    case HV_3D_BYTE_CUBE_PALETTE_HEAT:
      settings->palette = HV_3D_BYTE_CUBE_PALETTE_ASCII;
      break;
    case HV_3D_BYTE_CUBE_PALETTE_ASCII:
      settings->palette = HV_3D_BYTE_CUBE_PALETTE_MONO;
      break;
    default:
      settings->palette = HV_3D_BYTE_CUBE_PALETTE_RGB;
      break;
  }
}

static void hv_3d_adjust_byte_cube_drag(Hv3DByteCubeViewSettings *settings, int dx, int dy)
{
  if (settings == 0) {
    return;
  }

  settings->brightness += ((float)dy * 0.0035f);
  settings->contrast += ((float)dx * 0.0100f);
  if (settings->brightness < -0.95f) {
    settings->brightness = -0.95f;
  } else if (settings->brightness > 0.95f) {
    settings->brightness = 0.95f;
  }
  if (settings->contrast < 0.10f) {
    settings->contrast = 0.10f;
  } else if (settings->contrast > 4.00f) {
    settings->contrast = 4.00f;
  }
}

static void hv_3d_adjust_byte_cube_keys(Hv3DByteCubeViewSettings *settings, float brightness_delta, float contrast_delta)
{
  if (settings == 0) {
    return;
  }

  settings->brightness += brightness_delta;
  settings->contrast += contrast_delta;
  if (settings->brightness < -0.95f) {
    settings->brightness = -0.95f;
  } else if (settings->brightness > 0.95f) {
    settings->brightness = 0.95f;
  }
  if (settings->contrast < 0.10f) {
    settings->contrast = 0.10f;
  } else if (settings->contrast > 4.00f) {
    settings->contrast = 4.00f;
  }
}

int hv_3d_platform_apply_byte_cube_control(Hv3DByteCubeViewSettings *settings, Hv3DByteCubeControl control)
{
  if (settings == 0) {
    return 0;
  }

  switch (control) {
    case HV_3D_BYTE_CUBE_CONTROL_BLEND_ACCUMULATE:
      settings->blend_mode = HV_3D_BYTE_CUBE_BLEND_ACCUMULATE;
      return 1;
    case HV_3D_BYTE_CUBE_CONTROL_BLEND_ALPHA:
      settings->blend_mode = HV_3D_BYTE_CUBE_BLEND_ALPHA;
      return 1;
    case HV_3D_BYTE_CUBE_CONTROL_CYCLE_PALETTE:
      hv_3d_cycle_palette(settings);
      return 1;
    case HV_3D_BYTE_CUBE_CONTROL_BRIGHTNESS_STANDARD:
      settings->brightness = 0.0f;
      return 1;
    case HV_3D_BYTE_CUBE_CONTROL_BRIGHTNESS_LOW:
      settings->brightness = -0.08f;
      return 1;
    case HV_3D_BYTE_CUBE_CONTROL_BRIGHTNESS_INCREASE:
      hv_3d_adjust_byte_cube_keys(settings, 0.02f, 0.0f);
      return 1;
    case HV_3D_BYTE_CUBE_CONTROL_BRIGHTNESS_DECREASE:
      hv_3d_adjust_byte_cube_keys(settings, -0.02f, 0.0f);
      return 1;
    case HV_3D_BYTE_CUBE_CONTROL_CONTRAST_INCREASE:
      hv_3d_adjust_byte_cube_keys(settings, 0.0f, 0.08f);
      return 1;
    case HV_3D_BYTE_CUBE_CONTROL_CONTRAST_DECREASE:
      hv_3d_adjust_byte_cube_keys(settings, 0.0f, -0.08f);
      return 1;
    case HV_3D_BYTE_CUBE_CONTROL_INTENSITY_NEAREST:
      settings->interpolation = HV_3D_BYTE_CUBE_INTERPOLATION_NEAREST;
      return 1;
    case HV_3D_BYTE_CUBE_CONTROL_INTENSITY_LINEAR:
      settings->interpolation = HV_3D_BYTE_CUBE_INTERPOLATION_LINEAR;
      return 1;
    case HV_3D_BYTE_CUBE_CONTROL_POSITION_NEAREST:
      settings->position_interpolation = HV_3D_BYTE_CUBE_INTERPOLATION_NEAREST;
      return 1;
    case HV_3D_BYTE_CUBE_CONTROL_POSITION_LINEAR:
      settings->position_interpolation = HV_3D_BYTE_CUBE_INTERPOLATION_LINEAR;
      return 1;
    case HV_3D_BYTE_CUBE_CONTROL_RESET:
      hv_3d_byte_cube_view_settings_init_defaults(settings);
      return 1;
    default:
      return 0;
  }
}

int hv_3d_platform_reset_byte_cube_view(
  Hv3DByteCubeViewSettings *settings,
  Hv3DCamera *camera,
  const HvByteCube3D *cube
)
{
  uint32_t viewport_width = 0u;
  uint32_t viewport_height = 0u;

  if ((settings == 0) || (camera == 0) || (cube == 0)) {
    return 0;
  }

  viewport_width = camera->viewport_width;
  viewport_height = camera->viewport_height;
  hv_3d_byte_cube_view_settings_init_defaults(settings);
  hv_3d_camera_init_defaults(camera);
  (void)hv_3d_camera_set_viewport(camera, viewport_width, viewport_height);
  return hv_3d_camera_fit_byte_cube_overview(camera, cube);
}

static int hv_3d_apply_byte_cube_key(Hv3DByteCubeViewSettings *settings, SDL_Keycode key)
{
  switch (key) {
    case SDLK_a:
      return hv_3d_platform_apply_byte_cube_control(settings, HV_3D_BYTE_CUBE_CONTROL_BLEND_ACCUMULATE);
    case SDLK_b:
      return hv_3d_platform_apply_byte_cube_control(settings, HV_3D_BYTE_CUBE_CONTROL_BLEND_ALPHA);
    case SDLK_c:
      return hv_3d_platform_apply_byte_cube_control(settings, HV_3D_BYTE_CUBE_CONTROL_CYCLE_PALETTE);
    case SDLK_g:
      return hv_3d_platform_apply_byte_cube_control(settings, HV_3D_BYTE_CUBE_CONTROL_BRIGHTNESS_STANDARD);
    case SDLK_l:
      return hv_3d_platform_apply_byte_cube_control(settings, HV_3D_BYTE_CUBE_CONTROL_BRIGHTNESS_LOW);
    case SDLK_UP:
      return hv_3d_platform_apply_byte_cube_control(settings, HV_3D_BYTE_CUBE_CONTROL_BRIGHTNESS_INCREASE);
    case SDLK_DOWN:
      return hv_3d_platform_apply_byte_cube_control(settings, HV_3D_BYTE_CUBE_CONTROL_BRIGHTNESS_DECREASE);
    case SDLK_RIGHT:
      return hv_3d_platform_apply_byte_cube_control(settings, HV_3D_BYTE_CUBE_CONTROL_CONTRAST_INCREASE);
    case SDLK_LEFT:
      return hv_3d_platform_apply_byte_cube_control(settings, HV_3D_BYTE_CUBE_CONTROL_CONTRAST_DECREASE);
    case SDLK_p:
      return hv_3d_platform_apply_byte_cube_control(settings, HV_3D_BYTE_CUBE_CONTROL_INTENSITY_NEAREST);
    case SDLK_s:
      return hv_3d_platform_apply_byte_cube_control(settings, HV_3D_BYTE_CUBE_CONTROL_INTENSITY_LINEAR);
    case SDLK_n:
      return hv_3d_platform_apply_byte_cube_control(settings, HV_3D_BYTE_CUBE_CONTROL_POSITION_NEAREST);
    case SDLK_f:
      return hv_3d_platform_apply_byte_cube_control(settings, HV_3D_BYTE_CUBE_CONTROL_POSITION_LINEAR);
    case SDLK_r:
      return hv_3d_platform_apply_byte_cube_control(settings, HV_3D_BYTE_CUBE_CONTROL_RESET);
    default:
      return 0;
  }
}

static int hv_3d_platform_render_scene(
  const void *scene,
  const Hv3DCamera *camera,
  const Hv3DByteCubeViewSettings *byte_cube_settings,
  float point_size,
  unsigned int scene_kind,
  char *err,
  size_t err_size
)
{
  SDL_Window *window = 0;
  SDL_GLContext context = 0;
  Hv3DRenderer renderer;
  Hv3DCamera camera_state;
  Hv3DByteCubeViewSettings settings_state;
  Hv3DViewerState viewer_state;
  uint32_t render_frames = 0u;
  uint32_t frames_drawn = 0u;
  int hidden_window = 0;
  int running = 1;
  int need_video_quit = 0;
  Uint32 window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;

  if ((scene == 0) || (camera == 0)) {
    hv_set_error(err, err_size, "invalid arguments for 3D platform render");
    return 0;
  }
  if ((scene_kind != HV_3D_PLATFORM_SCENE_CLOUD) && (scene_kind != HV_3D_PLATFORM_SCENE_BYTE_CUBE)) {
    hv_set_error(err, err_size, "invalid 3D platform scene kind");
    return 0;
  }
  if (!hv_3d_renderer_validate_point_size(point_size, err, err_size)) {
    return 0;
  }
  if (!hv_3d_platform_viewer_available()) {
    hv_set_error(err, err_size, "3D viewer path is not available in this build");
    return 0;
  }
  if ((scene_kind == HV_3D_PLATFORM_SCENE_BYTE_CUBE) &&
      !hv_3d_byte_cube_view_settings_validate(byte_cube_settings, err, err_size)) {
    return 0;
  }

  memset(&renderer, 0, sizeof(renderer));
  memset(&viewer_state, 0, sizeof(viewer_state));
  memset(&settings_state, 0, sizeof(settings_state));
  camera_state = *camera;
  if (byte_cube_settings != 0) {
    settings_state = *byte_cube_settings;
  }
  (void)hv_3d_camera_set_viewport(&camera_state, camera->viewport_width, camera->viewport_height);
  viewer_state.viewport_width = camera_state.viewport_width;
  viewer_state.viewport_height = camera_state.viewport_height;
  viewer_state.byte_cube_overview_active =
    (scene_kind == HV_3D_PLATFORM_SCENE_BYTE_CUBE) &&
    (settings_state.projection == HV_3D_BYTE_CUBE_PROJECTION_FREE_3D);
  viewer_state.redraw_needed = 1;
  hidden_window = hv_parse_truthy_env("HILBERTVIZ3D_WINDOW_HIDDEN");
  if (!hv_parse_u32_env("HILBERTVIZ3D_RENDER_FRAMES", &render_frames, err, err_size)) {
    return 0;
  }

  if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0u) {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
      hv_set_error(err, err_size, "failed to initialize SDL video: %s", SDL_GetError());
      return 0;
    }
    need_video_quit = 1;
  }

  (void)SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  (void)SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  (void)SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  (void)SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  if (hidden_window) {
    window_flags |= SDL_WINDOW_HIDDEN;
  }

  window = SDL_CreateWindow(
    "hilbertviz3d",
    SDL_WINDOWPOS_CENTERED,
    SDL_WINDOWPOS_CENTERED,
    (int)camera->viewport_width,
    (int)camera->viewport_height,
    window_flags
  );
  if (window == 0) {
    hv_set_error(err, err_size, "failed to create 3D window: %s", SDL_GetError());
    if (need_video_quit) {
      SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    return 0;
  }

  context = SDL_GL_CreateContext(window);
  if (context == 0) {
    hv_set_error(err, err_size, "failed to create OpenGL context: %s", SDL_GetError());
    SDL_DestroyWindow(window);
    if (need_video_quit) {
      SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    return 0;
  }

  if (SDL_GL_MakeCurrent(window, context) != 0) {
    hv_set_error(err, err_size, "failed to activate OpenGL context: %s", SDL_GetError());
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    if (need_video_quit) {
      SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    return 0;
  }

  (void)SDL_GL_SetSwapInterval(0);

  if (scene_kind == HV_3D_PLATFORM_SCENE_CLOUD) {
    if (!hv_3d_renderer_init(&renderer, (const HvPointCloud3D *)scene, err, err_size)) {
      SDL_GL_DeleteContext(context);
      SDL_DestroyWindow(window);
      if (need_video_quit) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
      }
      return 0;
    }
  } else {
    if (!hv_3d_renderer_init_byte_cube(&renderer, (const HvByteCube3D *)scene, &settings_state, err, err_size)) {
      SDL_GL_DeleteContext(context);
      SDL_DestroyWindow(window);
      if (need_video_quit) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
      }
      return 0;
    }
  }

  while (running) {
    SDL_Event event;
    int had_event = 0;

    while (SDL_PollEvent(&event) != 0) {
      had_event = 1;
      if (event.type == SDL_QUIT) {
        running = 0;
      } else if ((event.type == SDL_KEYDOWN) && (event.key.keysym.sym == SDLK_ESCAPE)) {
        running = 0;
      } else if ((scene_kind == HV_3D_PLATFORM_SCENE_BYTE_CUBE) &&
                 (event.type == SDL_KEYDOWN) &&
                 (event.key.keysym.sym == SDLK_r)) {
        if (!hv_3d_platform_reset_byte_cube_view(&settings_state, &camera_state, (const HvByteCube3D *)scene)) {
          hv_3d_renderer_shutdown(&renderer);
          SDL_GL_DeleteContext(context);
          SDL_DestroyWindow(window);
          if (need_video_quit) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
          }
          hv_set_error(err, err_size, "failed to reset byte-cube view");
          return 0;
        }
        viewer_state.byte_cube_overview_active = 1;
        viewer_state.redraw_needed = 1;
      } else if ((scene_kind == HV_3D_PLATFORM_SCENE_BYTE_CUBE) &&
                 (event.type == SDL_KEYDOWN) &&
                 hv_3d_apply_byte_cube_key(&settings_state, event.key.keysym.sym)) {
        viewer_state.redraw_needed = 1;
      } else if ((event.type == SDL_MOUSEBUTTONDOWN) && (event.button.button == SDL_BUTTON_LEFT)) {
        viewer_state.dragging_left = 1;
        viewer_state.last_mouse_x = event.button.x;
        viewer_state.last_mouse_y = event.button.y;
      } else if ((scene_kind == HV_3D_PLATFORM_SCENE_BYTE_CUBE) &&
                 (event.type == SDL_MOUSEBUTTONDOWN) &&
                 (event.button.button == SDL_BUTTON_RIGHT)) {
        viewer_state.dragging_right = 1;
        viewer_state.last_mouse_x = event.button.x;
        viewer_state.last_mouse_y = event.button.y;
      } else if ((event.type == SDL_MOUSEBUTTONUP) && (event.button.button == SDL_BUTTON_LEFT)) {
        viewer_state.dragging_left = 0;
      } else if ((scene_kind == HV_3D_PLATFORM_SCENE_BYTE_CUBE) &&
                 (event.type == SDL_MOUSEBUTTONUP) &&
                 (event.button.button == SDL_BUTTON_RIGHT)) {
        viewer_state.dragging_right = 0;
      } else if ((event.type == SDL_MOUSEMOTION) && viewer_state.dragging_left) {
        int dx = event.motion.x - viewer_state.last_mouse_x;
        int dy = event.motion.y - viewer_state.last_mouse_y;

        viewer_state.last_mouse_x = event.motion.x;
        viewer_state.last_mouse_y = event.motion.y;
        (void)hv_3d_camera_orbit(&camera_state, (float)dx, (float)dy);
        viewer_state.byte_cube_overview_active = 0;
        viewer_state.redraw_needed = 1;
      } else if ((scene_kind == HV_3D_PLATFORM_SCENE_BYTE_CUBE) &&
                 (event.type == SDL_MOUSEMOTION) &&
                 viewer_state.dragging_right) {
        int dx = event.motion.x - viewer_state.last_mouse_x;
        int dy = event.motion.y - viewer_state.last_mouse_y;

        viewer_state.last_mouse_x = event.motion.x;
        viewer_state.last_mouse_y = event.motion.y;
        hv_3d_adjust_byte_cube_drag(&settings_state, dx, dy);
        viewer_state.redraw_needed = 1;
      } else if (event.type == SDL_MOUSEWHEEL) {
        float wheel_delta = (float)event.wheel.y;

#if SDL_VERSION_ATLEAST(2, 0, 4)
        if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
          wheel_delta = -wheel_delta;
        }
#endif
        if (wheel_delta != 0.0f) {
          (void)hv_3d_camera_zoom(&camera_state, wheel_delta);
          viewer_state.byte_cube_overview_active = 0;
          viewer_state.redraw_needed = 1;
        }
      } else if (event.type == SDL_WINDOWEVENT) {
        if (
          (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) ||
          (event.window.event == SDL_WINDOWEVENT_RESIZED)
        ) {
          viewer_state.viewport_width = (event.window.data1 > 0) ? (uint32_t)event.window.data1 : 1u;
          viewer_state.viewport_height = (event.window.data2 > 0) ? (uint32_t)event.window.data2 : 1u;
          if ((scene_kind == HV_3D_PLATFORM_SCENE_BYTE_CUBE) &&
              viewer_state.byte_cube_overview_active &&
              (settings_state.projection == HV_3D_BYTE_CUBE_PROJECTION_FREE_3D)) {
            (void)hv_3d_camera_set_viewport(
              &camera_state,
              viewer_state.viewport_width,
              viewer_state.viewport_height
            );
            if (!hv_3d_camera_fit_byte_cube_overview(&camera_state, (const HvByteCube3D *)scene)) {
              hv_3d_renderer_shutdown(&renderer);
              SDL_GL_DeleteContext(context);
              SDL_DestroyWindow(window);
              if (need_video_quit) {
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
              }
              hv_set_error(err, err_size, "failed to refit byte-cube overview on resize");
              return 0;
            }
          } else if ((scene_kind == HV_3D_PLATFORM_SCENE_BYTE_CUBE) &&
                     (settings_state.projection == HV_3D_BYTE_CUBE_PROJECTION_FREE_3D)) {
            if (!hv_3d_camera_preserve_scale_on_resize(
                  &camera_state,
                  viewer_state.viewport_width,
                  viewer_state.viewport_height
                )) {
              hv_3d_renderer_shutdown(&renderer);
              SDL_GL_DeleteContext(context);
              SDL_DestroyWindow(window);
              if (need_video_quit) {
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
              }
              hv_set_error(err, err_size, "failed to preserve byte-cube scale on resize");
              return 0;
            }
          } else {
            (void)hv_3d_camera_set_viewport(
              &camera_state,
              viewer_state.viewport_width,
              viewer_state.viewport_height
            );
          }
          viewer_state.redraw_needed = 1;
        } else if (
          (event.window.event == SDL_WINDOWEVENT_EXPOSED) ||
          (event.window.event == SDL_WINDOWEVENT_SHOWN)
        ) {
          viewer_state.redraw_needed = 1;
        }
      }
    }

    if (viewer_state.redraw_needed || ((render_frames > 0u) && (frames_drawn < render_frames))) {
      if (
        ((scene_kind == HV_3D_PLATFORM_SCENE_CLOUD) &&
         !hv_3d_renderer_draw(
           &renderer,
           &camera_state,
           viewer_state.viewport_width,
           viewer_state.viewport_height,
           point_size,
           err,
           err_size
         )) ||
        ((scene_kind == HV_3D_PLATFORM_SCENE_BYTE_CUBE) &&
         !hv_3d_renderer_draw_byte_cube(
           &renderer,
           &camera_state,
           &settings_state,
           viewer_state.viewport_width,
           viewer_state.viewport_height,
           err,
           err_size
         ))
      ) {
        hv_3d_renderer_shutdown(&renderer);
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        if (need_video_quit) {
          SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
        return 0;
      }

      SDL_GL_SwapWindow(window);
      viewer_state.redraw_needed = 0;
      ++frames_drawn;
      if ((render_frames > 0u) && (frames_drawn >= render_frames)) {
        running = 0;
      }
    } else if (!had_event) {
      SDL_Delay(16u);
    }
  }

  hv_3d_renderer_shutdown(&renderer);
  SDL_GL_DeleteContext(context);
  SDL_DestroyWindow(window);
  if (need_video_quit) {
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
  }

  return 1;
}
#endif

#if !defined(HV_3D_VIEWER_AVAILABLE)
static void hv_3d_cycle_palette(Hv3DByteCubeViewSettings *settings)
{
  if (settings == 0) {
    return;
  }

  switch (settings->palette) {
    case HV_3D_BYTE_CUBE_PALETTE_RGB:
      settings->palette = HV_3D_BYTE_CUBE_PALETTE_HEAT;
      break;
    case HV_3D_BYTE_CUBE_PALETTE_HEAT:
      settings->palette = HV_3D_BYTE_CUBE_PALETTE_ASCII;
      break;
    case HV_3D_BYTE_CUBE_PALETTE_ASCII:
      settings->palette = HV_3D_BYTE_CUBE_PALETTE_MONO;
      break;
    default:
      settings->palette = HV_3D_BYTE_CUBE_PALETTE_RGB;
      break;
  }
}

static void hv_3d_adjust_byte_cube_keys(Hv3DByteCubeViewSettings *settings, float brightness_delta, float contrast_delta)
{
  if (settings == 0) {
    return;
  }

  settings->brightness += brightness_delta;
  settings->contrast += contrast_delta;
  if (settings->brightness < -0.95f) {
    settings->brightness = -0.95f;
  } else if (settings->brightness > 0.95f) {
    settings->brightness = 0.95f;
  }
  if (settings->contrast < 0.10f) {
    settings->contrast = 0.10f;
  } else if (settings->contrast > 4.00f) {
    settings->contrast = 4.00f;
  }
}

int hv_3d_platform_apply_byte_cube_control(Hv3DByteCubeViewSettings *settings, Hv3DByteCubeControl control)
{
  if (settings == 0) {
    return 0;
  }

  switch (control) {
    case HV_3D_BYTE_CUBE_CONTROL_BLEND_ACCUMULATE:
      settings->blend_mode = HV_3D_BYTE_CUBE_BLEND_ACCUMULATE;
      return 1;
    case HV_3D_BYTE_CUBE_CONTROL_BLEND_ALPHA:
      settings->blend_mode = HV_3D_BYTE_CUBE_BLEND_ALPHA;
      return 1;
    case HV_3D_BYTE_CUBE_CONTROL_CYCLE_PALETTE:
      hv_3d_cycle_palette(settings);
      return 1;
    case HV_3D_BYTE_CUBE_CONTROL_BRIGHTNESS_STANDARD:
      settings->brightness = 0.0f;
      return 1;
    case HV_3D_BYTE_CUBE_CONTROL_BRIGHTNESS_LOW:
      settings->brightness = -0.08f;
      return 1;
    case HV_3D_BYTE_CUBE_CONTROL_BRIGHTNESS_INCREASE:
      hv_3d_adjust_byte_cube_keys(settings, 0.02f, 0.0f);
      return 1;
    case HV_3D_BYTE_CUBE_CONTROL_BRIGHTNESS_DECREASE:
      hv_3d_adjust_byte_cube_keys(settings, -0.02f, 0.0f);
      return 1;
    case HV_3D_BYTE_CUBE_CONTROL_CONTRAST_INCREASE:
      hv_3d_adjust_byte_cube_keys(settings, 0.0f, 0.08f);
      return 1;
    case HV_3D_BYTE_CUBE_CONTROL_CONTRAST_DECREASE:
      hv_3d_adjust_byte_cube_keys(settings, 0.0f, -0.08f);
      return 1;
    case HV_3D_BYTE_CUBE_CONTROL_INTENSITY_NEAREST:
      settings->interpolation = HV_3D_BYTE_CUBE_INTERPOLATION_NEAREST;
      return 1;
    case HV_3D_BYTE_CUBE_CONTROL_INTENSITY_LINEAR:
      settings->interpolation = HV_3D_BYTE_CUBE_INTERPOLATION_LINEAR;
      return 1;
    case HV_3D_BYTE_CUBE_CONTROL_POSITION_NEAREST:
      settings->position_interpolation = HV_3D_BYTE_CUBE_INTERPOLATION_NEAREST;
      return 1;
    case HV_3D_BYTE_CUBE_CONTROL_POSITION_LINEAR:
      settings->position_interpolation = HV_3D_BYTE_CUBE_INTERPOLATION_LINEAR;
      return 1;
    case HV_3D_BYTE_CUBE_CONTROL_RESET:
      hv_3d_byte_cube_view_settings_init_defaults(settings);
      return 1;
    default:
      return 0;
  }
}

int hv_3d_platform_reset_byte_cube_view(
  Hv3DByteCubeViewSettings *settings,
  Hv3DCamera *camera,
  const HvByteCube3D *cube
)
{
  uint32_t viewport_width = 0u;
  uint32_t viewport_height = 0u;

  if ((settings == 0) || (camera == 0) || (cube == 0)) {
    return 0;
  }

  viewport_width = camera->viewport_width;
  viewport_height = camera->viewport_height;
  hv_3d_byte_cube_view_settings_init_defaults(settings);
  hv_3d_camera_init_defaults(camera);
  (void)hv_3d_camera_set_viewport(camera, viewport_width, viewport_height);
  return hv_3d_camera_fit_byte_cube_overview(camera, cube);
}
#endif

int hv_3d_platform_render_static_cloud(
  const HvPointCloud3D *cloud,
  const Hv3DCamera *camera,
  float point_size,
  char *err,
  size_t err_size
)
{
#if defined(HV_3D_VIEWER_AVAILABLE)
  return hv_3d_platform_render_scene(
    cloud,
    camera,
    0,
    point_size,
    HV_3D_PLATFORM_SCENE_CLOUD,
    err,
    err_size
  );
#else
  if (!hv_3d_renderer_validate_point_size(point_size, err, err_size)) {
    return 0;
  }
  if ((cloud == 0) || (camera == 0)) {
    hv_set_error(err, err_size, "invalid arguments for 3D platform render");
    return 0;
  }

  hv_set_error(err, err_size, "3D viewer path is not available in this build");
  return 0;
#endif
}

int hv_3d_platform_render_static_byte_cube(
  const HvByteCube3D *cube,
  const Hv3DCamera *camera,
  const Hv3DByteCubeViewSettings *settings,
  char *err,
  size_t err_size
)
{
#if defined(HV_3D_VIEWER_AVAILABLE)
  return hv_3d_platform_render_scene(
    cube,
    camera,
    settings,
    HV_3D_POINT_SIZE_DEFAULT,
    HV_3D_PLATFORM_SCENE_BYTE_CUBE,
    err,
    err_size
  );
#else
  if ((cube == 0) || (camera == 0) || (settings == 0)) {
    hv_set_error(err, err_size, "invalid arguments for 3D byte-cube platform render");
    return 0;
  }
  if (!hv_3d_byte_cube_view_settings_validate(settings, err, err_size)) {
    return 0;
  }

  hv_set_error(err, err_size, "3D viewer path is not available in this build");
  return 0;
#endif
}
