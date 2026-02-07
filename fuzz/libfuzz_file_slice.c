#include "fuzz_target.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  return hv_fuzz_file_slice(data, size);
}
