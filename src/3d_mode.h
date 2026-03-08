#ifndef HV_3D_MODE_H
#define HV_3D_MODE_H

typedef enum Hv3DMode {
  HV_3D_MODE_HILBERT = 0,
  HV_3D_MODE_BYTE_CUBE = 1
} Hv3DMode;

const char *hv_3d_mode_name(Hv3DMode mode);
int hv_3d_parse_mode(const char *text, Hv3DMode *mode_out);

#endif
