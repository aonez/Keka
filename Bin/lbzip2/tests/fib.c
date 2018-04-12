/* Copyright (C) 2011 Mikolaj Izdebski */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char **argv)
{
  char *p, *q, *r, *s;
  size_t req_sz;

  errno = 0;
  req_sz = 900000;
  if (argc > 1)
    req_sz = strtol(argv[1], 0, 10);
  if (errno != 0) {
    perror("strtol");
    return EXIT_FAILURE;
  }

  p = q = r = s = malloc(req_sz+1);
  if (p == 0) {
    perror("malloc");
    exit(EXIT_FAILURE);
  }

  r = q + req_sz;
  *q++ = 'a';
  while (q < r) {
    if (*p++ == 'a') *q++ = 'b';
    *q++ = 'a';
  }

  if (fwrite(s, req_sz, 1, stdout) != 1) {
    perror("fwrite");
    return EXIT_FAILURE;
  }

  free(s);
  return EXIT_SUCCESS;
}
