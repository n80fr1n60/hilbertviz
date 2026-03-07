#include "hilbert3d.h"

#include <stdio.h>
#include <string.h>

static void hv3d_print_usage(const char *prog)
{
  (void)printf(
    "Usage: %s [--help]\n"
    "\n"
    "Experimental 3D Hilbert scaffold.\n"
    "Current status: the 3D mapping core is implemented and tested, but the\n"
    "interactive viewer is not wired yet.\n"
    "Supported order range: %u..%u\n",
    prog,
    HV_HILBERT3D_MIN_ORDER,
    HV_HILBERT3D_MAX_ORDER
  );
}

int main(int argc, char **argv)
{
  if ((argc == 2) && ((strcmp(argv[1], "--help") == 0) || (strcmp(argv[1], "-h") == 0))) {
    hv3d_print_usage(argv[0]);
    return 0;
  }

  if (argc == 1) {
    hv3d_print_usage(argv[0]);
    return 0;
  }

  (void)fprintf(stderr, "hilbertviz3d: unsupported arguments. Use --help.\n");
  return 1;
}
