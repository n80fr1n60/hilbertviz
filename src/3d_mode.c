#include "3d_mode.h"

#include <string.h>

const char *hv_3d_mode_name(Hv3DMode mode)
{
  switch (mode) {
    case HV_3D_MODE_HILBERT:
      return "hilbert";
    case HV_3D_MODE_BYTE_CUBE:
      return "byte-cube";
    default:
      return "unknown";
  }
}

int hv_3d_parse_mode(const char *text, Hv3DMode *mode_out)
{
  if ((text == 0) || (mode_out == 0)) {
    return 0;
  }

  if (strcmp(text, "hilbert") == 0) {
    *mode_out = HV_3D_MODE_HILBERT;
    return 1;
  }
  if (strcmp(text, "byte-cube") == 0) {
    *mode_out = HV_3D_MODE_BYTE_CUBE;
    return 1;
  }

  return 0;
}
