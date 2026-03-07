#ifndef POINT_CLOUD3D_H
#define POINT_CLOUD3D_H

#include <stddef.h>
#include <stdint.h>

typedef struct HvPoint3D {
  float x;
  float y;
  float z;
  uint8_t r;
  uint8_t g;
  uint8_t b;
} HvPoint3D;

typedef struct HvPointCloud3D {
  HvPoint3D *points;
  size_t count;
  uint32_t order;
  uint32_t side;
  uint64_t capacity;
} HvPointCloud3D;

int hv_build_point_cloud3d(
  const char *input_path,
  uint32_t order,
  uint64_t offset,
  int has_length,
  uint64_t length,
  HvPointCloud3D *cloud_out,
  char *err,
  size_t err_size
);

void hv_free_point_cloud3d(HvPointCloud3D *cloud);

#endif
