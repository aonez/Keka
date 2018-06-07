/*-
  common.h -- common declarations

  Copyright (C) 2012 Mikolaj Izdebski

  This file is part of lbzip2.

  lbzip2 is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  lbzip2 is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with lbzip2.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>             /* assert() */
#include <errno.h>              /* errno */
#include <inttypes.h>           /* uint32_t */
#include <stdbool.h>            /* bool */
#include <stddef.h>             /* size_t */
#include <stdlib.h>             /* abort() */

#include "xalloc.h"             /* XMALLOC() */


/* Tracing, useful in debugging, but not officially supported. */
#ifdef ENABLE_TRACING
#define Trace(x) info x
#else
#define Trace(x)
#endif


/*
  Minimal and maximal alphabet size used in prefix coding.  We always have 2
  RLE symbols, from 0 to 255 MTF values and 1 EOF symbol.
*/
#define MIN_ALPHA_SIZE (2+0+1)
#define MAX_ALPHA_SIZE (2+255+1)

#define MIN_TREES 2
#define MAX_TREES 6
#define GROUP_SIZE 50
#define MIN_CODE_LENGTH 1       /* implied by MIN_ALPHA_SIZE > 1u */
#define MAX_CODE_LENGTH 20
#define MAX_BLOCK_SIZE 900000
#define MAX_GROUPS ((MAX_BLOCK_SIZE + GROUP_SIZE - 1) / GROUP_SIZE)
#define MAX_SELECTORS 32767

enum error {
  OK,                           /* no error */
  MORE,
  FINISH,

  ERR_MAGIC,                    /* bad stream header magic */
  ERR_HEADER,                   /* bad block header magic */
  ERR_BITMAP,                   /* empty source alphabet */
  ERR_TREES,                    /* bad number of trees */
  ERR_GROUPS,                   /* no coding groups */
  ERR_SELECTOR,                 /* invalid selector */
  ERR_DELTA,                    /* invalid delta code */
  ERR_PREFIX,                   /* invalid prefix code */
  ERR_INCOMPLT,                 /* incomplete prefix code */
  ERR_EMPTY,                    /* empty block */
  ERR_UNTERM,                   /* unterminated block */
  ERR_RUNLEN,                   /* missing run length */
  ERR_BLKCRC,                   /* block CRC mismatch */
  ERR_STRMCRC,                  /* stream CRC mismatch */
  ERR_OVERFLOW,                 /* block overflow */
  ERR_BWTIDX,                   /* primary index too large */
  ERR_EOF,                      /* unexpected end of file */
};


/* Minimum and maximum. It's important to keep the same condition in both
   macros because then some compilers on some architectures (like gcc on x86)
   will generate better code. */
#define min(x,y) ((x) < (y) ? (x) : (y))
#define max(x,y) ((x) < (y) ? (y) : (x))


/* Check GCC version.  This not only works for GNU C, but also Clang and
   possibly others.  If a particular compiler defines __GNUC__ but it's not GCC
   compatible then it's that compilers problem. */
#define GNUC_VERSION (10000 * (__GNUC__ + 0) + 100 * (__GNUC_MINOR__ + 0) + \
                      (__GNUC_PATCHLEVEL__ + 0))


/* Explicit static branch prediction to help compiler generating faster
   code. */
#if GNUC_VERSION >= 30004
# define likely(x)   __builtin_expect((x), 1)
# define unlikely(x) __builtin_expect((x), 0)
#else
# define likely(x)   (x)
# define unlikely(x) (x)
#endif


#ifndef __GNUC__
#define __attribute__(x)
#endif
