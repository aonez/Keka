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
#include "encoder.h"


const CRC32 crc32;


int LZ_encoder::get_match_pairs( Pair * pairs )
  {
  int len_limit = match_len_limit;
  if( len_limit > available_bytes() )
    {
    len_limit = available_bytes();
    if( len_limit < 4 ) return 0;
    }

  int maxlen = 0;
  int num_pairs = 0;
  const int pos1 = pos + 1;
  const int min_pos = ( pos > dictionary_size ) ? pos - dictionary_size : 0;
  const uint8_t * const data = ptr_to_current_pos();

  unsigned tmp = crc32[data[0]] ^ data[1];
  const int key2 = tmp & ( num_prev_positions2 - 1 );
  tmp ^= (unsigned)data[2] << 8;
  const int key3 = num_prev_positions2 + ( tmp & ( num_prev_positions3 - 1 ) );
  const int key4 = num_prev_positions23 +
                   ( ( tmp ^ ( crc32[data[3]] << 5 ) ) & key4_mask );

  if( pairs )
    {
    int np2 = prev_positions[key2];
    int np3 = prev_positions[key3];
    if( np2 > min_pos && buffer[np2-1] == data[0] )
      {
      pairs[0].dis = pos - np2;
      pairs[0].len = maxlen = 2;
      num_pairs = 1;
      }
    if( np2 != np3 && np3 > min_pos && buffer[np3-1] == data[0] )
      {
      maxlen = 3;
      np2 = np3;
      pairs[num_pairs].dis = pos - np2;
      ++num_pairs;
      }
    if( num_pairs > 0 )
      {
      const int delta = pos1 - np2;
      while( maxlen < len_limit && data[maxlen-delta] == data[maxlen] )
        ++maxlen;
      pairs[num_pairs-1].len = maxlen;
      if( maxlen >= len_limit ) pairs = 0;	// done. now just skip
      }
    if( maxlen < 3 ) maxlen = 3;
    }

  prev_positions[key2] = pos1;
  prev_positions[key3] = pos1;
  int newpos1 = prev_positions[key4];
  prev_positions[key4] = pos1;

  int32_t * ptr0 = pos_array + ( cyclic_pos << 1 );
  int32_t * ptr1 = ptr0 + 1;
  int len = 0, len0 = 0, len1 = 0;

  for( int count = cycles; ; )
    {
    if( newpos1 <= min_pos || --count < 0 ) { *ptr0 = *ptr1 = 0; break; }

    const int delta = pos1 - newpos1;
    int32_t * const newptr = pos_array +
      ( ( cyclic_pos - delta +
          ( ( cyclic_pos >= delta ) ? 0 : dictionary_size + 1 ) ) << 1 );
    if( data[len-delta] == data[len] )
      {
      while( ++len < len_limit && data[len-delta] == data[len] ) {}
      if( pairs && maxlen < len )
        {
        pairs[num_pairs].dis = delta - 1;
        pairs[num_pairs].len = maxlen = len;
        ++num_pairs;
        }
      if( len >= len_limit )
        {
        *ptr0 = newptr[0];
        *ptr1 = newptr[1];
        break;
        }
      }
    if( data[len-delta] < data[len] )
      {
      *ptr0 = newpos1;
      ptr0 = newptr + 1;
      newpos1 = *ptr0;
      len0 = len; if( len1 < len ) len = len1;
      }
    else
      {
      *ptr1 = newpos1;
      ptr1 = newptr;
      newpos1 = *ptr1;
      len1 = len; if( len0 < len ) len = len0;
      }
    }
  return num_pairs;
  }


void LZ_encoder::update_distance_prices()
  {
  for( int dis = start_dis_model; dis < modeled_distances; ++dis )
    {
    const int dis_slot = dis_slots[dis];
    const int direct_bits = ( dis_slot >> 1 ) - 1;
    const int base = ( 2 | ( dis_slot & 1 ) ) << direct_bits;
    const int price = price_symbol_reversed( bm_dis + ( base - dis_slot ),
                                             dis - base, direct_bits );
    for( int len_state = 0; len_state < len_states; ++len_state )
      dis_prices[len_state][dis] = price;
    }

  for( int len_state = 0; len_state < len_states; ++len_state )
    {
    int * const dsp = dis_slot_prices[len_state];
    const Bit_model * const bmds = bm_dis_slot[len_state];
    int slot = 0;
    for( ; slot < end_dis_model; ++slot )
      dsp[slot] = price_symbol6( bmds, slot );
    for( ; slot < num_dis_slots; ++slot )
      dsp[slot] = price_symbol6( bmds, slot ) +
                  (((( slot >> 1 ) - 1 ) - dis_align_bits ) << price_shift_bits );

    int * const dp = dis_prices[len_state];
    int dis = 0;
    for( ; dis < start_dis_model; ++dis )
      dp[dis] = dsp[dis];
    for( ; dis < modeled_distances; ++dis )
      dp[dis] += dsp[dis_slots[dis]];
    }
  }


/* Returns the number of bytes advanced (ahead).
   trials[0]..trials[ahead-1] contain the steps to encode.
   ( trials[0].dis4 == -1 ) means literal.
   A match/rep longer or equal than match_len_limit finishes the sequence.
*/
int LZ_encoder::sequence_optimizer( const int reps[num_rep_distances],
                                    const State state )
  {
  int num_pairs, num_trials;

  if( pending_num_pairs > 0 )			// from previous call
    {
    num_pairs = pending_num_pairs;
    pending_num_pairs = 0;
    }
  else
    num_pairs = read_match_distances();
  const int main_len = ( num_pairs > 0 ) ? pairs[num_pairs-1].len : 0;

  int replens[num_rep_distances];
  int rep_index = 0;
  for( int i = 0; i < num_rep_distances; ++i )
    {
    replens[i] = true_match_len( 0, reps[i] + 1 );
    if( replens[i] > replens[rep_index] ) rep_index = i;
    }
  if( replens[rep_index] >= match_len_limit )
    {
    trials[0].price = replens[rep_index];
    trials[0].dis4 = rep_index;
    move_and_update( replens[rep_index] );
    return replens[rep_index];
    }

  if( main_len >= match_len_limit )
    {
    trials[0].price = main_len;
    trials[0].dis4 = pairs[num_pairs-1].dis + num_rep_distances;
    move_and_update( main_len );
    return main_len;
    }

  const int pos_state = data_position() & pos_state_mask;
  const uint8_t prev_byte = peek( 1 );
  const uint8_t cur_byte = peek( 0 );
  const uint8_t match_byte = peek( reps[0] + 1 );

  trials[1].price = price0( bm_match[state()][pos_state] );
  if( state.is_char() )
    trials[1].price += price_literal( prev_byte, cur_byte );
  else
    trials[1].price += price_matched( prev_byte, cur_byte, match_byte );
  trials[1].dis4 = -1;					// literal

  const int match_price = price1( bm_match[state()][pos_state] );
  const int rep_match_price = match_price + price1( bm_rep[state()] );

  if( match_byte == cur_byte )
    trials[1].update( rep_match_price + price_shortrep( state, pos_state ), 0, 0 );

  num_trials = std::max( main_len, replens[rep_index] );

  if( num_trials < min_match_len )
    {
    trials[0].price = 1;
    trials[0].dis4 = trials[1].dis4;
    move_pos();
    return 1;
    }

  trials[0].state = state;
  for( int i = 0; i < num_rep_distances; ++i )
    trials[0].reps[i] = reps[i];

  for( int len = min_match_len; len <= num_trials; ++len )
    trials[len].price = infinite_price;

  for( int rep = 0; rep < num_rep_distances; ++rep )
    {
    if( replens[rep] < min_match_len ) continue;
    const int price = rep_match_price + price_rep( rep, state, pos_state );
    for( int len = min_match_len; len <= replens[rep]; ++len )
      trials[len].update( price + rep_len_prices.price( len, pos_state ),
                          rep, 0 );
    }

  if( main_len > replens[0] )
    {
    const int normal_match_price = match_price + price0( bm_rep[state()] );
    int i = 0, len = std::max( replens[0] + 1, (int)min_match_len );
    while( len > pairs[i].len ) ++i;
    while( true )
      {
      const int dis = pairs[i].dis;
      trials[len].update( normal_match_price + price_pair( dis, len, pos_state ),
                          dis + num_rep_distances, 0 );
      if( ++len > pairs[i].len && ++i >= num_pairs ) break;
      }
    }

  int cur = 0;
  while( true )				// price optimization loop
    {
    move_pos();
    if( ++cur >= num_trials )		// no more initialized trials
      {
      backward( cur );
      return cur;
      }

    const int num_pairs = read_match_distances();
    const int newlen = ( num_pairs > 0 ) ? pairs[num_pairs-1].len : 0;
    if( newlen >= match_len_limit )
      {
      pending_num_pairs = num_pairs;
      backward( cur );
      return cur;
      }

    // give final values to current trial
    Trial & cur_trial = trials[cur];
    State cur_state;
    {
    const int dis4 = cur_trial.dis4;
    int prev_index = cur_trial.prev_index;
    const int prev_index2 = cur_trial.prev_index2;

    if( prev_index2 == single_step_trial )
      {
      cur_state = trials[prev_index].state;
      if( prev_index + 1 == cur )			// len == 1
        {
        if( dis4 == 0 ) cur_state.set_short_rep();
        else cur_state.set_char();			// literal
        }
      else if( dis4 < num_rep_distances ) cur_state.set_rep();
      else cur_state.set_match();
      }
    else
      {
      if( prev_index2 == dual_step_trial )	// dis4 == 0 (rep0)
        --prev_index;
      else					// prev_index2 >= 0
        prev_index = prev_index2;
      cur_state.set_char_rep();
      }
    cur_trial.state = cur_state;
    for( int i = 0; i < num_rep_distances; ++i )
      cur_trial.reps[i] = trials[prev_index].reps[i];
    mtf_reps( dis4, cur_trial.reps );		// literal is ignored
    }

    const int pos_state = data_position() & pos_state_mask;
    const uint8_t prev_byte = peek( 1 );
    const uint8_t cur_byte = peek( 0 );
    const uint8_t match_byte = peek( cur_trial.reps[0] + 1 );

    int next_price = cur_trial.price +
                     price0( bm_match[cur_state()][pos_state] );
    if( cur_state.is_char() )
      next_price += price_literal( prev_byte, cur_byte );
    else
      next_price += price_matched( prev_byte, cur_byte, match_byte );

    // try last updates to next trial
    Trial & next_trial = trials[cur+1];

    next_trial.update( next_price, -1, cur );		// literal

    const int match_price = cur_trial.price + price1( bm_match[cur_state()][pos_state] );
    const int rep_match_price = match_price + price1( bm_rep[cur_state()] );

    if( match_byte == cur_byte && next_trial.dis4 != 0 &&
        next_trial.prev_index2 == single_step_trial )
      {
      const int price = rep_match_price + price_shortrep( cur_state, pos_state );
      if( price <= next_trial.price )
        {
        next_trial.price = price;
        next_trial.dis4 = 0;				// rep0
        next_trial.prev_index = cur;
        }
      }

    const int triable_bytes =
      std::min( available_bytes(), max_num_trials - 1 - cur );
    if( triable_bytes < min_match_len ) continue;

    const int len_limit = std::min( match_len_limit, triable_bytes );

    // try literal + rep0
    if( match_byte != cur_byte && next_trial.prev_index != cur )
      {
      const uint8_t * const data = ptr_to_current_pos();
      const int dis = cur_trial.reps[0] + 1;
      const int limit = std::min( match_len_limit + 1, triable_bytes );
      int len = 1;
      while( len < limit && data[len-dis] == data[len] ) ++len;
      if( --len >= min_match_len )
        {
        const int pos_state2 = ( pos_state + 1 ) & pos_state_mask;
        State state2 = cur_state; state2.set_char();
        const int price = next_price +
                          price1( bm_match[state2()][pos_state2] ) +
                          price1( bm_rep[state2()] ) +
                          price_rep0_len( len, state2, pos_state2 );
        while( num_trials < cur + 1 + len )
          trials[++num_trials].price = infinite_price;
        trials[cur+1+len].update2( price, cur + 1 );
        }
      }

    int start_len = min_match_len;

    // try rep distances
    for( int rep = 0; rep < num_rep_distances; ++rep )
      {
      const uint8_t * const data = ptr_to_current_pos();
      const int dis = cur_trial.reps[rep] + 1;
      int len;

      if( data[0-dis] != data[0] || data[1-dis] != data[1] ) continue;
      for( len = min_match_len; len < len_limit; ++len )
        if( data[len-dis] != data[len] ) break;
      while( num_trials < cur + len )
        trials[++num_trials].price = infinite_price;
      int price = rep_match_price + price_rep( rep, cur_state, pos_state );
      for( int i = min_match_len; i <= len; ++i )
        trials[cur+i].update( price + rep_len_prices.price( i, pos_state ),
                              rep, cur );

      if( rep == 0 ) start_len = len + 1;	// discard shorter matches

      // try rep + literal + rep0
      int len2 = len + 1;
      const int limit = std::min( match_len_limit + len2, triable_bytes );
      while( len2 < limit && data[len2-dis] == data[len2] ) ++len2;
      len2 -= len + 1;
      if( len2 < min_match_len ) continue;

      int pos_state2 = ( pos_state + len ) & pos_state_mask;
      State state2 = cur_state; state2.set_rep();
      price += rep_len_prices.price( len, pos_state ) +
               price0( bm_match[state2()][pos_state2] ) +
               price_matched( data[len-1], data[len], data[len-dis] );
      pos_state2 = ( pos_state2 + 1 ) & pos_state_mask;
      state2.set_char();
      price += price1( bm_match[state2()][pos_state2] ) +
               price1( bm_rep[state2()] ) +
               price_rep0_len( len2, state2, pos_state2 );
      while( num_trials < cur + len + 1 + len2 )
        trials[++num_trials].price = infinite_price;
      trials[cur+len+1+len2].update3( price, rep, cur + len + 1, cur );
      }

    // try matches
    if( newlen >= start_len && newlen <= len_limit )
      {
      const int normal_match_price = match_price +
                                     price0( bm_rep[cur_state()] );

      while( num_trials < cur + newlen )
        trials[++num_trials].price = infinite_price;

      int i = 0;
      while( pairs[i].len < start_len ) ++i;
      int dis = pairs[i].dis;
      for( int len = start_len; ; ++len )
        {
        int price = normal_match_price + price_pair( dis, len, pos_state );
        trials[cur+len].update( price, dis + num_rep_distances, cur );

        // try match + literal + rep0
        if( len == pairs[i].len )
          {
          const uint8_t * const data = ptr_to_current_pos();
          const int dis2 = dis + 1;
          int len2 = len + 1;
          const int limit = std::min( match_len_limit + len2, triable_bytes );
          while( len2 < limit && data[len2-dis2] == data[len2] ) ++len2;
          len2 -= len + 1;
          if( len2 >= min_match_len )
            {
            int pos_state2 = ( pos_state + len ) & pos_state_mask;
            State state2 = cur_state; state2.set_match();
            price += price0( bm_match[state2()][pos_state2] ) +
                     price_matched( data[len-1], data[len], data[len-dis2] );
            pos_state2 = ( pos_state2 + 1 ) & pos_state_mask;
            state2.set_char();
            price += price1( bm_match[state2()][pos_state2] ) +
                     price1( bm_rep[state2()] ) +
                     price_rep0_len( len2, state2, pos_state2 );

            while( num_trials < cur + len + 1 + len2 )
              trials[++num_trials].price = infinite_price;
            trials[cur+len+1+len2].update3( price, dis + num_rep_distances,
                                            cur + len + 1, cur );
            }
          if( ++i >= num_pairs ) break;
          dis = pairs[i].dis;
          }
        }
      }
    }
  }


bool LZ_encoder::encode_member( const unsigned long long member_size )
  {
  const unsigned long long member_size_limit =
    member_size - File_trailer::size - max_marker_size;
  const bool best = ( match_len_limit > 12 );
  const int dis_price_count = best ? 1 : 512;
  const int align_price_count = best ? 1 : dis_align_size;
  const int price_count = ( match_len_limit > 36 ) ? 1013 : 4093;
  int price_counter = 0;		// counters may decrement below 0
  int dis_price_counter = 0;
  int align_price_counter = 0;
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
    get_match_pairs();
    move_pos();
    }

  while( !data_finished() )
    {
    if( price_counter <= 0 && pending_num_pairs == 0 )
      {
      price_counter = price_count;	// recalculate prices every these bytes
      if( dis_price_counter <= 0 )
        { dis_price_counter = dis_price_count; update_distance_prices(); }
      if( align_price_counter <= 0 )
        {
        align_price_counter = align_price_count;
        for( int i = 0; i < dis_align_size; ++i )
          align_prices[i] = price_symbol_reversed( bm_align, i, dis_align_bits );
        }
      match_len_prices.update_prices();
      rep_len_prices.update_prices();
      }

    int ahead = sequence_optimizer( reps, state );
    price_counter -= ahead;

    for( int i = 0; ahead > 0; )
      {
      const int pos_state = ( data_position() - ahead ) & pos_state_mask;
      const int len = trials[i].price;
      int dis = trials[i].dis4;

      bool bit = ( dis < 0 );
      renc.encode_bit( bm_match[state()][pos_state], !bit );
      if( bit )					// literal byte
        {
        const uint8_t prev_byte = peek( ahead + 1 );
        const uint8_t cur_byte = peek( ahead );
        crc32.update_byte( crc_, cur_byte );
        if( state.is_char_set_char() )
          encode_literal( prev_byte, cur_byte );
        else
          {
          const uint8_t match_byte = peek( ahead + reps[0] + 1 );
          encode_matched( prev_byte, cur_byte, match_byte );
          }
        }
      else					// match or repeated match
        {
        crc32.update_buf( crc_, ptr_to_current_pos() - ahead, len );
        mtf_reps( dis, reps );
        bit = ( dis < num_rep_distances );
        renc.encode_bit( bm_rep[state()], bit );
        if( bit )				// repeated match
          {
          bit = ( dis == 0 );
          renc.encode_bit( bm_rep0[state()], !bit );
          if( bit )
            renc.encode_bit( bm_len[state()][pos_state], len > 1 );
          else
            {
            renc.encode_bit( bm_rep1[state()], dis > 1 );
            if( dis > 1 )
              renc.encode_bit( bm_rep2[state()], dis > 2 );
            }
          if( len == 1 ) state.set_short_rep();
          else
            {
            renc.encode_len( rep_len_model, len, pos_state );
            rep_len_prices.decrement_counter( pos_state );
            state.set_rep();
            }
          }
        else					// match
          {
          dis -= num_rep_distances;
          encode_pair( dis, len, pos_state );
          if( dis >= modeled_distances ) --align_price_counter;
          --dis_price_counter;
          match_len_prices.decrement_counter( pos_state );
          state.set_match();
          }
        }
      ahead -= len; i += len;
      if( renc.member_position() >= member_size_limit )
        {
        if( !dec_pos( ahead ) ) return false;
        full_flush( state );
        return true;
        }
      }
    }
  full_flush( state );
  return true;
  }
