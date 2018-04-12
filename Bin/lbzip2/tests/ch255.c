/* Copyright (C) 2011 Mikolaj Izdebski */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(int argc, char **argv)
{
  size_t req_sz;
  char buf[4096];

  errno = 0;
  req_sz = 900000;
  if (argc > 1)
    req_sz = strtol(argv[1], 0, 10);
  if (errno != 0) {
    perror("strtol");
    return EXIT_FAILURE;
  }

  memset(buf, 255, 4096);

  while (req_sz > 0) {
    size_t step = req_sz;
    if (step > 4096)
      step = 4096;
    req_sz -= step;

    if (fwrite(buf, step, 1, stdout) != 1) {
      perror("fwrite");
      return EXIT_FAILURE;
    }
  }

  return EXIT_SUCCESS;
}
