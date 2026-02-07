#include "fuzz_target.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define HV_AFL_MAX_INPUT (1u << 20u)
#define HV_AFL_PERSIST_ITERS 1000u

#ifndef __AFL_FUZZ_INIT
#define __AFL_FUZZ_INIT()
#define __AFL_INIT() \
  do {               \
  } while (0)
#define __AFL_LOOP(x) 0
#define __AFL_FUZZ_TESTCASE_BUF ((unsigned char *)0)
#define __AFL_FUZZ_TESTCASE_LEN 0u
#endif

static size_t hv_read_file(const char *path, uint8_t *buf, size_t cap)
{
  FILE *fp = 0;
  size_t total = 0u;

  if ((path == 0) || (buf == 0) || (cap == 0u)) {
    return 0u;
  }

  fp = fopen(path, "rb");
  if (fp == 0) {
    return 0u;
  }

  while (total < cap) {
    size_t n = fread(buf + total, 1u, cap - total, fp);
    if (n == 0u) {
      break;
    }
    total += n;
  }

  (void)fclose(fp);
  return total;
}

#ifdef __AFL_HAVE_MANUAL_CONTROL
__AFL_FUZZ_INIT();
#endif

int main(int argc, char **argv)
{
  unsigned char *fuzz_buf = 0;

  __AFL_INIT();
  fuzz_buf = __AFL_FUZZ_TESTCASE_BUF;

  if (fuzz_buf != 0) {
    while (__AFL_LOOP(HV_AFL_PERSIST_ITERS)) {
      size_t n = (size_t)__AFL_FUZZ_TESTCASE_LEN;
      if (n > HV_AFL_MAX_INPUT) {
        n = HV_AFL_MAX_INPUT;
      }
      (void)hv_fuzz_pipeline(fuzz_buf, n);
    }
    return 0;
  }

  if (argc >= 2) {
    uint8_t *buf = (uint8_t *)malloc(HV_AFL_MAX_INPUT);
    size_t n = 0u;

    if (buf == 0) {
      return 0;
    }

    n = hv_read_file(argv[1], buf, HV_AFL_MAX_INPUT);
    (void)hv_fuzz_pipeline(buf, n);
    free(buf);
  }
  return 0;
}
