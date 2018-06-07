/* Copyright (C) 2010 Mikolaj Izdebski */

#include <stdio.h>

unsigned b,k;

p(x,n) {
  b = (b << n) + x, k += n;
  while (k >= 8)
    putchar((b >> (k -= 8)) & 0xff);
}
pb(b) { p(b,8); }

run(unsigned long long n) {
  n--;
  while (1) {
    pb((int)(n&1));
    if (n<2) break;
    n=(n-2)/2;
  }
}

main() {
  int i,j;
  pb('B'), pb('Z'), pb('h'), pb('9');     /* magic */

  p(0x314159,24), p(0x265359, 24);        /* block header */
  p(0,16), p(0,16);                       /* block crc */
  p(0,1);   /* block randomised? */
  p(0,24);  /* bwt index */
  for (i=16+256-2; i--;) p(1,1);  /* bitmap */
  p(0,2);
  p(2,3), p(355,15);              /* 2 groups, 355 selectors */
  for (i=355; i--;) p(0,1);       /* all selectors equal to 0 */
  for (j=2; j--;) {               /* code lens for both groups */
    p(8,5);                       /* all codes are 8-bit */
    for (i=256; i--;) p(0,1);     /* delta */
  }

#define AMT 500000

  /* now: lit={0,0} ftab={0,0} */
  run((AMT/2+0x80000000) & 0xffffffff);
  /* now: lit={0,0} ftab={-BIG,0} */
  pb(2); /* 1 */
  /* now: lit={0,1} ftab={-BIG,1} */
  pb(2); /* 0 */
  /* now: lit={1,1} ftab={-BIG+1,1} */
  run((AMT/2+0x80000000) & 0xffffffff);
  /* now: lit={1,1} ftab={AMT+1,1} */
  pb(2); /* 1 */
  /* now: lit={1,2} ftab={AMT+1,2} */
  run(AMT);
  /* now: lit={1,AMT+2} ftab={AMT+1,AMT+2} */
  pb(2); /* 0 */
  /* now: lit={2,AMT+2} ftab={AMT+2,AMT+2} */
  pb(2); /* 1 */
  /* now: lit={2,AMT+3} ftab={AMT+2,AMT+3} */
  run(-AMT & 0xffffffff);
  /* now: lit={2,AMT+3} ftab={AMT+2,3} */

  pb(0xff);
  return 0;
}
