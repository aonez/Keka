/*-
  decode.h -- low-level decompressor header

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

struct header {
  int bs100k;
  uint32_t crc;
};


struct parser_state {
  int state;
  int bs100k;
  uint32_t stored_crc;
  uint32_t computed_crc;
};


struct in_blk;

struct bitstream {
  unsigned live;
  uint64_t buff;
  struct in_blk *block;
  const uint32_t *data;
  const uint32_t *limit;
  bool eof;
};


struct decoder_state {
  struct retriever_internal_state *internal_state;

  bool rand;                    /* block randomized */
  unsigned bwt_idx;             /* BWT primary index */
  unsigned block_size;          /* compressed block size */
  uint32_t crc;                 /* expected block CRC */
  uint32_t ftab[256];           /* frequency table used in counting sort */
  uint32_t *tt;

  int rle_state;                /* FSA state */
  uint32_t rle_crc;             /* CRC checksum */
  uint32_t rle_index;           /* IBWT linked list pointer */
  uint32_t rle_avail;           /* available input bytes */
  uint8_t rle_char;             /* current character */
  uint8_t rle_prev;             /* prevoius character */
};


struct source;

extern uint32_t crc_table[256];

void parser_init(struct parser_state *ps, int bs100k);
int parse(struct parser_state *ps, struct header *hd, struct bitstream *bs,
          unsigned *garbage);
int scan(struct bitstream *bs, unsigned skip);

void decoder_init(struct decoder_state *ds);
void decoder_free(struct decoder_state *ds);
int retrieve(struct decoder_state *ds, struct bitstream *bs);
void decode(struct decoder_state *ds);
int verify(struct decoder_state *ds);
int emit(struct decoder_state *ds, void *buf, size_t *buf_sz);
