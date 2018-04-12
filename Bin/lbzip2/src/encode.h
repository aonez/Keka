/*-
  encode.h -- low-level compressor header

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

#define CLUSTER_FACTOR  8u
#define HEADER_SIZE     4u
#define TRAILER_SIZE    10u


struct encoder_state;

struct encoder_state *encoder_init(unsigned long mbs, unsigned cf);
int collect(struct encoder_state *e, const uint8_t *buf, size_t *buf_sz);
size_t encode(struct encoder_state *e, uint32_t *crc);
void transmit(struct encoder_state *e, void *buf);
unsigned generate_prefix_code(struct encoder_state *s);

int32_t divbwt(uint8_t *T, int32_t *SA, int32_t n);

#define combine_crc(cc,c) (((cc) << 1) ^ ((cc) >> 31) ^ (c) ^ -1)
