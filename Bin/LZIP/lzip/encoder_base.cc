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


Dis_slots dis_slots;
Prob_prices prob_prices;


bool Matchfinder_base::read_block()
  {
  if( !at_stream_end && stream_pos < buffer_size )
    {
    const int size = buffer_size - stream_pos;
    const int rd = readblock( infd, buffer + stream_pos, size );
    stream_pos += rd;
    if( rd != size && errno ) throw Error( "Read error" );
    if( rd < size ) { at_stream_end = true; pos_limit = buffer_size; }
    }
  return pos < stream_pos;
  }


void Matchfinder_base::normalize_pos()
  {
  if( pos > stream_pos )
    internal_error( "pos > stream_pos in Matchfinder_base::normalize_pos." );
  if( !at_stream_end )
    {
    const int offset = pos - before_size - dictionary_size;
    const int size = stream_pos - offset;
    std::memmove( buffer, buffer + offset, size );
    partial_data_pos += offset;
    pos -= offset;		// pos = before_size + dictionary_size
    stream_pos -= offset;
    for( int i = 0; i < num_prev_positions; ++i )
      prev_positions[i] -= std::min( prev_positions[i], offset );
    for( int i = 0; i < pos_array_size; ++i )
      pos_array[i] -= std::min( pos_array[i], offset );
    read_block();
    }
  }


Matchfinder_base::Matchfinder_base( const int before, const int dict_size,
                    const int after_size, const int dict_factor,
                    const int num_prev_positions23,
                    const int pos_array_factor, const int ifd )
  :
  partial_data_pos( 0 ),
  before_size( before ),
  pos( 0 ),
  cyclic_pos( 0 ),
  stream_pos( 0 ),
  infd( ifd ),
  at_stream_end( false )
  {
  const int buffer_size_limit =
    ( dict_factor * dict_size ) + before_size + after_size;
  buffer_size = std::max( 65536, dict_size );
  buffer = (uint8_t *)std::malloc( buffer_size );
  if( !buffer ) throw std::bad_alloc();
  if( read_block() && !at_stream_end && buffer_size < buffer_size_limit )
    {
    buffer_size = buffer_size_limit;
    uint8_t * const tmp = (uint8_t *)std::realloc( buffer, buffer_size );
    if( !tmp ) { std::free( buffer ); throw std::bad_alloc(); }
    buffer = tmp;
    read_block();
    }
  if( at_stream_end && stream_pos < dict_size )
    dictionary_size = std::max( (int)min_dictionary_size, stream_pos );
  else
    dictionary_size = dict_size;
  pos_limit = buffer_size;
  if( !at_stream_end ) pos_limit -= after_size;
  unsigned size = 1 << std::max( 16, real_bits( dictionary_size - 1 ) - 2 );
  if( dictionary_size > 1 << 26 )		// 64 MiB
    size >>= 1;
  key4_mask = size - 1;
  size += num_prev_positions23;

  num_prev_positions = size;
  pos_array_size = pos_array_factor * ( dictionary_size + 1 );
  size += pos_array_size;
  if( size * sizeof prev_positions[0] <= size ) prev_positions = 0;
  else prev_positions = new( std::nothrow ) int32_t[size];
  if( !prev_positions ) { std::free( buffer ); throw std::bad_alloc(); }
  pos_array = prev_positions + num_prev_positions;
  for( int i = 0; i < num_prev_positions; ++i ) prev_positions[i] = 0;
  }


void Matchfinder_base::reset()
  {
  if( stream_pos > pos )
    std::memmove( buffer, buffer + pos, stream_pos - pos );
  partial_data_pos = 0;
  stream_pos -= pos;
  pos = 0;
  cyclic_pos = 0;
  for( int i = 0; i < num_prev_positions; ++i ) prev_positions[i] = 0;
  read_block();
  }


void Range_encoder::flush_data()
  {
  if( pos > 0 )
    {
    if( outfd >= 0 && writeblock( outfd, buffer, pos ) != pos )
      throw Error( "Write error" );
    partial_member_pos += pos;
    pos = 0;
    show_progress();
    }
  }


     // End Of Stream mark => (dis == 0xFFFFFFFFU, len == min_match_len)
void LZ_encoder_base::full_flush( const State state )
  {
  const int pos_state = data_position() & pos_state_mask;
  renc.encode_bit( bm_match[state()][pos_state], 1 );
  renc.encode_bit( bm_rep[state()], 0 );
  encode_pair( 0xFFFFFFFFU, min_match_len, pos_state );
  renc.flush();
  File_trailer trailer;
  trailer.data_crc( crc() );
  trailer.data_size( data_position() );
  trailer.member_size( renc.member_position() + File_trailer::size );
  for( int i = 0; i < File_trailer::size; ++i )
    renc.put_byte( trailer.data[i] );
  renc.flush_data();
  }


void LZ_encoder_base::reset()
  {
  Matchfinder_base::reset();
  crc_ = 0xFFFFFFFFU;
  bm_literal[0][0].reset( ( 1 << literal_context_bits ) * 0x300 );
  bm_match[0][0].reset( State::states * pos_states );
  bm_rep[0].reset( State::states );
  bm_rep0[0].reset( State::states );
  bm_rep1[0].reset( State::states );
  bm_rep2[0].reset( State::states );
  bm_len[0][0].reset( State::states * pos_states );
  bm_dis_slot[0][0].reset( len_states * (1 << dis_slot_bits ) );
  bm_dis[0].reset( modeled_distances - end_dis_model + 1 );
  bm_align[0].reset( dis_align_size );
  match_len_model.reset();
  rep_len_model.reset();
  renc.reset();
  }
