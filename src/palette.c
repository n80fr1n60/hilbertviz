#include "palette.h"

static uint8_t hv_scale_channel(uint8_t min_v, uint8_t max_v, uint32_t pos, uint32_t span)
{
  uint32_t min_u = (uint32_t)min_v;
  uint32_t max_u = (uint32_t)max_v;
  uint32_t delta = max_u - min_u;
  uint32_t scaled = 0;

  if (span == 0u) {
    return max_v;
  }

  scaled = min_u + ((pos * delta) + (span / 2u)) / span;
  return (uint8_t)scaled;
}

void hv_byte_to_rgb(uint8_t value, uint8_t rgb_out[3])
{
  rgb_out[0] = 0u;
  rgb_out[1] = 0u;
  rgb_out[2] = 0u;

  if (value == 0x00u) {
    return;
  }

  if (value <= 0x1Fu) {
    rgb_out[1] = hv_scale_channel(32u, 255u, (uint32_t)(value - 0x01u), (uint32_t)(0x1Fu - 0x01u));
    return;
  }

  if (value <= 0x7Eu) {
    rgb_out[2] = hv_scale_channel(32u, 255u, (uint32_t)(value - 0x20u), (uint32_t)(0x7Eu - 0x20u));
    return;
  }

  rgb_out[0] = hv_scale_channel(32u, 255u, (uint32_t)(value - 0x7Fu), (uint32_t)(0xFFu - 0x7Fu));
}
