#include "3d_platform.h"

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
    return "available (SDL2/OpenGL detected; render loop not wired yet)";
  }
  if (hv_3d_platform_viewer_requested()) {
    return "unavailable (SDL2/OpenGL development packages not found)";
  }
  return "disabled at build time";
}
