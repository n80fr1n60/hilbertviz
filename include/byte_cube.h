#ifndef BYTE_CUBE_H
#define BYTE_CUBE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define HV_BYTE_CUBE_SIDE 256u
#define HV_BYTE_CUBE_TOTAL_VOXELS (16777216ULL)
#define HV_BYTE_CUBE_VOLUME_BYTES ((uint64_t)HV_BYTE_CUBE_TOTAL_VOXELS * (uint64_t)sizeof(uint32_t))

typedef struct HvByteCube3D {
  uint32_t *voxels;
  uint32_t side;
  uint64_t total_voxels;
  uint64_t trigram_count;
  uint64_t occupied_voxels;
  uint8_t occupied_min_x;
  uint8_t occupied_min_y;
  uint8_t occupied_min_z;
  uint8_t occupied_max_x;
  uint8_t occupied_max_y;
  uint8_t occupied_max_z;
  uint32_t max_density;
  uint8_t max_x;
  uint8_t max_y;
  uint8_t max_z;
} HvByteCube3D;

int hv_build_byte_cube3d(
  const char *input_path,
  uint64_t offset,
  int has_length,
  uint64_t length,
  HvByteCube3D *cube_out,
  char *err,
  size_t err_size
);

int hv_byte_cube_increment_trigram(
  HvByteCube3D *cube,
  uint8_t x,
  uint8_t y,
  uint8_t z,
  char *err,
  size_t err_size
);

int hv_byte_cube_default_slices(
  const HvByteCube3D *cube,
  float *slice_x,
  float *slice_y,
  float *slice_z,
  char *err,
  size_t err_size
);

int hv_write_byte_cube3d_summary(
  FILE *stream,
  const HvByteCube3D *cube,
  char *err,
  size_t err_size
);

void hv_free_byte_cube3d(HvByteCube3D *cube);

#endif
