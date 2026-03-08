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
  int dragging;
  int last_mouse_x;
  int last_mouse_y;
  uint32_t viewport_width;
  uint32_t viewport_height;
  int redraw_needed;
} Hv3DViewerState;

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

  SDL_Window *window = 0;
  SDL_GLContext context = 0;
  Hv3DRenderer renderer;
  Hv3DCamera camera_state;
  Hv3DViewerState viewer_state;
  uint32_t render_frames = 0u;
  uint32_t frames_drawn = 0u;
  int hidden_window = 0;
  int running = 1;
  int need_video_quit = 0;
  Uint32 window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;

  if ((cloud == 0) || (camera == 0)) {
    hv_set_error(err, err_size, "invalid arguments for 3D platform render");
    return 0;
  }
  if (!hv_3d_renderer_validate_point_size(point_size, err, err_size)) {
    return 0;
  }
  if (!hv_3d_platform_viewer_available()) {
    hv_set_error(err, err_size, "3D viewer path is not available in this build");
    return 0;
  }

  memset(&renderer, 0, sizeof(renderer));
  memset(&viewer_state, 0, sizeof(viewer_state));
  camera_state = *camera;
  (void)hv_3d_camera_set_viewport(&camera_state, camera->viewport_width, camera->viewport_height);
  viewer_state.viewport_width = camera_state.viewport_width;
  viewer_state.viewport_height = camera_state.viewport_height;
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

  if (!hv_3d_renderer_init(&renderer, cloud, err, err_size)) {
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    if (need_video_quit) {
      SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    return 0;
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
      } else if ((event.type == SDL_MOUSEBUTTONDOWN) && (event.button.button == SDL_BUTTON_LEFT)) {
        viewer_state.dragging = 1;
        viewer_state.last_mouse_x = event.button.x;
        viewer_state.last_mouse_y = event.button.y;
      } else if ((event.type == SDL_MOUSEBUTTONUP) && (event.button.button == SDL_BUTTON_LEFT)) {
        viewer_state.dragging = 0;
      } else if ((event.type == SDL_MOUSEMOTION) && viewer_state.dragging) {
        int dx = event.motion.x - viewer_state.last_mouse_x;
        int dy = event.motion.y - viewer_state.last_mouse_y;

        viewer_state.last_mouse_x = event.motion.x;
        viewer_state.last_mouse_y = event.motion.y;
        (void)hv_3d_camera_orbit(&camera_state, (float)dx, (float)dy);
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
          viewer_state.redraw_needed = 1;
        }
      } else if (event.type == SDL_WINDOWEVENT) {
        if (
          (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) ||
          (event.window.event == SDL_WINDOWEVENT_RESIZED)
        ) {
          viewer_state.viewport_width = (event.window.data1 > 0) ? (uint32_t)event.window.data1 : 1u;
          viewer_state.viewport_height = (event.window.data2 > 0) ? (uint32_t)event.window.data2 : 1u;
          (void)hv_3d_camera_set_viewport(
            &camera_state,
            viewer_state.viewport_width,
            viewer_state.viewport_height
          );
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
        !hv_3d_renderer_draw(
          &renderer,
          &camera_state,
          viewer_state.viewport_width,
          viewer_state.viewport_height,
          point_size,
          err,
          err_size
        )
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
