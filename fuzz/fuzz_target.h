#ifndef HV_FUZZ_TARGET_H
#define HV_FUZZ_TARGET_H

#include <stddef.h>
#include <stdint.h>

int hv_fuzz_pipeline(const uint8_t *data, size_t size);
int hv_fuzz_file_slice(const uint8_t *data, size_t size);

#endif
