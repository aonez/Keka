/*  Lzip - LZMA lossless data compressor
    Copyright (C) 2008-2017 Antonio Diaz Diaz.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#define _FILE_OFFSET_BITS 64

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <stdint.h>

#include "lzip.h"
#include "encoder_base.h"
#include "fast_encoder.h"


int FLZ_encoder::longest_match_len( int * const distance )
  {
  enum { len_limit = 16 };
  const int available = std::min( available_bytes(), (int)max_match_len );
  if( available < len_limit ) return 0;

  const uint8_t * const data = ptr_to_current_pos();
  key4 = ( ( key4 << 4 ) ^ data[3] ) & key4_mask;
  const int pos1 = pos + 1;
  int newpos1 = prev_positions[key4];
  prev_positions[key4] = pos1;
  int32_t * ptr0 = pos_array + cyclic_pos;
  int maxlen = 0;

  for( int count = 4; ; )
    {
    int delta;
    if( newpos1 <= 0 || --count < 0 ||
        ( delta = pos1 - newpos1 ) > dictionary_size ) { *ptr0 = 0; break; }
    int32_t * const newptr = pos_array +
      ( cyclic_pos - delta +
          ( ( cyclic_pos >= delta ) ? 0 : dictionary_size + 1 ) );

    if( data[maxlen-delta] == data[maxlen] )
      {
      int len = 0;
      while( len < available && data[len-delta] == data[len] ) ++len;
      if( maxlen < len )
        { maxlen = len; *distance = delta - 1;
          if( maxlen >= len_limit ) { *ptr0 = *newptr; break; } }
      }

    *ptr0 = newpos1;
    ptr0 = newptr;
    newpos1 = *ptr0;
    }
  return maxlen;
  }


bool FLZ_encoder::encode_member( const unsigned long long member_size )
  {
  const unsigned long long member_size_limit =
    member_size - File_trailer::size - max_marker_size;
  int rep = 0;
  int reps[num_rep_distances];
  State state;
  for( int i = 0; i < num_rep_distances; ++i ) reps[i] = 0;

  if( data_position() != 0 || renc.member_position() != File_header::size )
    return false;				// can be called only once

  if( !data_finished() )			// encode first byte
    {
    const uint8_t prev_byte = 0;
    const uint8_t cur_byte = peek( 0 );
    renc.encode_bit( bm_match[state()][0], 0 );
    encode_literal( prev_byte, cur_byte );
    crc32.update_byte( crc_, cur_byte );
    reset_key4();
    update_and_move( 1 );
    }

  while( !data_finished() && renc.member_position() < member_size_limit )
    {
    int match_distance;
    const int main_len = longest_match_len( &match_distance );
    const int pos_state = data_position() & pos_state_mask;
    int len = 0;

    for( int i = 0; i < num_rep_distances; ++i )
      {
      const int tlen = true_match_len( 0, reps[i] + 1 );
      if( tlen > len ) { len = tlen; rep = i; }
      }
    if( len > min_match_len && len + 3 > main_len )
      {
      crc32.update_buf( crc_, ptr_to_current_pos(), len );
      renc.encode_bit( bm_match[state()][pos_state], 1 );
      renc.encode_bit( bm_rep[state()], 1 );
      renc.encode_bit( bm_rep0[state()], rep != 0 );
      if( rep == 0 )
        renc.encode_bit( bm_len[state()][pos_state], 1 );
      else
        {
        renc.encode_bit( bm_rep1[state()], rep > 1 );
        if( rep > 1 )
          renc.encode_bit( bm_rep2[state()], rep > 2 );
        const int distance = reps[rep];
        for( int i = rep; i > 0; --i ) reps[i] = reps[i-1];
        reps[0] = distance;
        }
      state.set_rep();
      renc.encode_len( rep_len_model, len, pos_state );
      move_pos();
      update_and_move( len - 1 );
      continue;
      }

    if( main_len > min_match_len )
      {
      crc32.update_buf( crc_, ptr_to_current_pos(), main_len );
      renc.encode_bit( bm_match[state()][pos_state], 1 );
      renc.encode_bit( bm_rep[state()], 0 );
      state.set_match();
      for( int i = num_rep_distances - 1; i > 0; --i ) reps[i] = reps[i-1];
      reps[0] = match_distance;
      encode_pair( match_distance, main_len, pos_state );
      move_pos();
      update_and_move( main_len - 1 );
      continue;
      }

    const uint8_t prev_byte = peek( 1 );
    const uint8_t cur_byte = peek( 0 );
    const uint8_t match_byte = peek( reps[0] + 1 );
    move_pos();
    crc32.update_byte( crc_, cur_byte );

    if( match_byte == cur_byte )
      {
      const int short_rep_price = price1( bm_match[state()][pos_state] ) +
                                  price1( bm_rep[state()] ) +
                                  price0( bm_rep0[state()] ) +
                                  price0( bm_len[state()][pos_state] );
      int price = price0( bm_match[state()][pos_state] );
      if( state.is_char() )
        price += price_literal( prev_byte, cur_byte );
      else
        price += price_matched( prev_byte, cur_byte, match_byte );
      if( short_rep_price < price )
        {
        renc.encode_bit( bm_match[state()][pos_state], 1 );
        renc.encode_bit( bm_rep[state()], 1 );
        renc.encode_bit( bm_rep0[state()], 0 );
        renc.encode_bit( bm_len[state()][pos_state], 0 );
        state.set_short_rep();
        continue;
        }
      }

    // literal byte
    renc.encode_bit( bm_match[state()][pos_state], 0 );
    if( state.is_char_set_char() )
      encode_literal( prev_byte, cur_byte );
    else
      encode_matched( prev_byte, cur_byte, match_byte );
    }

  full_flush( state );
  return true;
  }
