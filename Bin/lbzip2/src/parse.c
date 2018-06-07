/*-
  parse.c -- find block boundaries

  Copyright (C) 2011, 2012, 2013, 2014 Mikolaj Izdebski

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

/*
  This file contains implementations of two different algorithms for finding
  boundaries of compressed block -- deterministic "parsing" and probabilistic
  "scanning".  Both are based on deterministic finite state automatons.

  Parsing is a deterministic algorithm used to find location of next block,
  given location of the end of previous block.  It processes and validates
  block and stream metadata.  Its main disadvantage is that it has to be ran
  sequentially only -- all preceding blocks must be fully decoded before
  attempting to find the location of any block.

  Scanning is another way of finding locations where compressed blocks are
  likely to begin.  It can be ran in parallel by multiple threads to discover
  locations which with high probability start compressed blocks.

  The two ways of finding block boundaries can be nicely combined together.
  First scanning can be used to determine likely candidates for blocks, which
  can be later confirmed or rejected after parsing block headers.

  The probability of finding false positives when scanning for block headers is
  usually very small (below 1e-14).  However it is possible to create
  compressed files which would cause large number of false positives, so lbzip2
  had to be designed to behave correctly even in cases with high ratios of
  false positives.  There is also some small probability of not discovering
  existing block header in the scanning phase.  This can happen if block magic
  pattern, which is used to identify block headers, crosses boundaries of two
  I/O blocks.  Such headers will not be recognized by scanner threads.  The
  probability of missing magic pattern is low -- the magic is only 6 bytes long
  and I/O blocks are 1 MiB each.  This is not a big problem as deterministic
  parser will will eventually find all missed blocks, but it is possible to
  construct a compressed file in a way which will prevent scanner from finding
  some blocks.  Both above cases of specially hand-crafted compressed files may
  affect decompression performance, but they should not affect correctness.

  lbzip2 was constructed in the way that worst-case decompression time using
  more than one thread is the same as decompression using one thread, minus the
  time spent for thread synchronization, which is usually low.  (Simply
  speaking, a n-thread decompressor can be thought of as a single sequential
  decompressor with n-1 helper workers, which try to predict what will need to
  be done in future and prepare that work in advance so that when sequential
  decompressor reaches that point it will be able to simply take the work done
  by helper worker.  The predictions are usually close to be perfect, which
  means that the sequential worker can take advantage of them, which results in
  performance gains.  But if some evil force tries to mislead helper workers,
  the worst that can happen is complete waste of resources used by the n-1
  helper threads -- the deterministic sequential decompressor will still be
  able to complete the decompression job, without any help from other workers.)
*/


#include "common.h"             /* OK */

#include <arpa/inet.h>          /* htonl() */

#include "main.h"               /* Trace() */
#include "decode.h"             /* bits_need() */

#include "scantab.h"


#define bits_need(bs,n)                         \
  ((n) <= (bs)->live                            \
   ?                                            \
   OK                                           \
   :                                            \
   unlikely ((bs)->data == (bs)->limit)         \
   ?                                            \
   (bs)->eof                                    \
   ?                                            \
   FINISH                                       \
   :                                            \
   MORE                                         \
   :                                            \
   ((bs)->buff |= (uint64_t) ntohl              \
    (*(bs)->data) << (32u - (bs)->live),        \
    (bs)->data++,                               \
    (bs)->live += 32u,                          \
    OK))

#define bits_peek(bs,n) ((bs)->buff >> (64u - (n)))

#define bits_dump(bs,n)                         \
  ((bs)->buff <<= (n),                          \
   (bs)->live -= (n),                           \
   (void) 0)

#define bits_align(bs)                          \
  (bits_dump (bs, (bs)->live % 8u),             \
   (void) 0)

#define bits_consume(bs)                        \
  (bits_dump (bs, (bs)->live),                  \
   (bs)->data = (bs)->limit,                    \
   (void) 0)


enum {
  STREAM_MAGIC_1, STREAM_MAGIC_2, BLOCK_MAGIC_1, BLOCK_MAGIC_2, BLOCK_MAGIC_3,
  BLOCK_CRC_1, BLOCK_CRC_2, EOS_2, EOS_3, EOS_CRC_1, EOS_CRC_2,
};


void
parser_init(struct parser_state *ps, int my_bs100k)
{
  ps->state = BLOCK_MAGIC_1;
  ps->bs100k = my_bs100k;
  ps->computed_crc = 0u;
}


/* Parse stream headers until a compressed block or end of stream is reached.

   Possible return codes:
     OK          - a compressed block was found
     FINISH      - end of stream was reached
     MORE        - more input is need, parsing was suspended
     ERR_HEADER  - invalid stream header
     ERR_STRMCRC - stream CRC does not match
     ERR_EOF     - unterminated stream (EOF reached before end of stream)

   garbage is set only when returning FINISH.  It is number of garbage bits
   consumed after end of stream was reached.
*/
int
parse(struct parser_state *restrict ps, struct header *restrict hd,
      struct bitstream *bs, unsigned *garbage)
{
  assert(ps->state != ACCEPT);

  while (OK == bits_need(bs, 16)) {
    unsigned word = bits_peek(bs, 16);

    bits_dump(bs, 16);

    switch (ps->state) {
    case STREAM_MAGIC_1:
      if (0x425Au != word) {
        hd->bs100k = -1;
        hd->crc = 0;
        ps->state = ACCEPT;
        *garbage = 16;
        return FINISH;
      }
      ps->state = STREAM_MAGIC_2;
      continue;

    case STREAM_MAGIC_2:
      if (0x6839u < word || 0x6831 > word) {
        hd->bs100k = -1;
        hd->crc = 0;
        ps->state = ACCEPT;
        *garbage = 32;
        return FINISH;
      }
      ps->bs100k = word & 15u;
      ps->state = BLOCK_MAGIC_1;
      continue;

    case BLOCK_MAGIC_1:
      if (0x1772u == word) {
        ps->state = EOS_2;
        continue;
      }
      if (0x3141u != word)
        return ERR_HEADER;
      ps->state = BLOCK_MAGIC_2;
      continue;

    case BLOCK_MAGIC_2:
      if (0x5926u != word)
        return ERR_HEADER;
      ps->state = BLOCK_MAGIC_3;
      continue;

    case BLOCK_MAGIC_3:
      if (0x5359u != word)
        return ERR_HEADER;
      ps->state = BLOCK_CRC_1;
      continue;

    case BLOCK_CRC_1:
      ps->stored_crc = word;
      ps->state = BLOCK_CRC_2;
      continue;

    case BLOCK_CRC_2:
      hd->crc = (ps->stored_crc << 16) | word;
      hd->bs100k = ps->bs100k;
      ps->computed_crc =
          (ps->computed_crc << 1) ^ (ps->computed_crc >> 31) ^ hd->crc;
      ps->state = BLOCK_MAGIC_1;
      return OK;

    case EOS_2:
      if (0x4538u != word)
        return ERR_HEADER;
      ps->state = EOS_3;
      continue;

    case EOS_3:
      if (0x5090u != word)
        return ERR_HEADER;
      ps->state = EOS_CRC_1;
      continue;

    case EOS_CRC_1:
      ps->stored_crc = word;
      ps->state = EOS_CRC_2;
      continue;

    case EOS_CRC_2:
      ps->stored_crc = (ps->stored_crc << 16) | word;
      if (ps->stored_crc != ps->computed_crc)
        return ERR_STRMCRC;
      ps->computed_crc = 0u;
      bits_align(bs);
      ps->state = STREAM_MAGIC_1;
      continue;

    default:
      break;
    }

    assert(0);
  }

  if (FINISH != bits_need(bs, 16))
    return MORE;

  if (ps->state == STREAM_MAGIC_1) {
    ps->state = ACCEPT;
    *garbage = 0;
    return FINISH;
  }
  if (ps->state == STREAM_MAGIC_2) {
    ps->state = ACCEPT;
    *garbage = 16;
    return FINISH;
  }

  return ERR_EOF;
}


/* Scan for magic bit sequence which presence indicates probable start of
   compressed block.

   Possible return codes:
     OK   - the magic sequence was found
     MORE - block header magic was not found
*/
int
scan(struct bitstream *bs, unsigned skip)
{
  unsigned state = 0;
  const uint32_t *data, *limit;

  if (skip > bs->live) {
    skip -= bs->live;
    bits_dump(bs, bs->live);
    skip = (skip + 31u) / 32u;
    if (bs->limit - bs->data < skip)
      bs->data = bs->limit;
    else
      bs->data += skip;
  }

again:
  assert(state < ACCEPT);
  while (bs->live > 0) {
    unsigned bit = bits_peek(bs, 1);

    bits_dump(bs, 1);
    state = mini_dfa[state][bit];

    if (state == ACCEPT) {
      if (bits_need(bs, 32) == OK) {
        bits_dump(bs, 32);
        return OK;
      }
      else {
        bits_consume(bs);
        return MORE;
      }
    }
  }

  data = bs->data;
  limit = bs->limit;

  while (data < limit) {
    unsigned bt_state = state;
    uint32_t word = *data;

    word = ntohl(word);
    state = big_dfa[state][word >> 24];
    state = big_dfa[state][(uint8_t)(word >> 16)];
    state = big_dfa[state][(uint8_t)(word >> 8)];
    state = big_dfa[state][(uint8_t)word];

    if (unlikely(state == ACCEPT)) {
      state = bt_state;
      bs->data = data;
      (void)bits_need(bs, 1u);
      goto again;
    }

    data++;
  }

  bs->data = data;
  return MORE;
}
