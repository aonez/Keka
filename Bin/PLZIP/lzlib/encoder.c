/*  Lzlib - Compression library for the lzip format
    Copyright (C) 2009-2017 Antonio Diaz Diaz.

    This library is free software. Redistribution and use in source and
    binary forms, with or without modification, are permitted provided
    that the following conditions are met:

    1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/

static int LZe_get_match_pairs( struct LZ_encoder * const e, struct Pair * pairs )
  {
  int32_t * ptr0 = e->eb.mb.pos_array + ( e->eb.mb.cyclic_pos << 1 );
  int32_t * ptr1 = ptr0 + 1;
  int32_t * newptr;
  int len = 0, len0 = 0, len1 = 0;
  int maxlen = 0;
  int num_pairs = 0;
  const int pos1 = e->eb.mb.pos + 1;
  const int min_pos = ( e->eb.mb.pos > e->eb.mb.dictionary_size ) ?
                        e->eb.mb.pos - e->eb.mb.dictionary_size : 0;
  const uint8_t * const data = Mb_ptr_to_current_pos( &e->eb.mb );
  int count, key2, key3, key4, newpos1;
  unsigned tmp;
  int len_limit = e->match_len_limit;

  if( len_limit > Mb_available_bytes( &e->eb.mb ) )
    {
    e->been_flushed = true;
    len_limit = Mb_available_bytes( &e->eb.mb );
    if( len_limit < 4 ) { *ptr0 = *ptr1 = 0; return 0; }
    }

  tmp = crc32[data[0]] ^ data[1];
  key2 = tmp & ( num_prev_positions2 - 1 );
  tmp ^= (unsigned)data[2] << 8;
  key3 = num_prev_positions2 + ( tmp & ( num_prev_positions3 - 1 ) );
  key4 = num_prev_positions2 + num_prev_positions3 +
         ( ( tmp ^ ( crc32[data[3]] << 5 ) ) & e->eb.mb.key4_mask );

  if( pairs )
    {
    int np2 = e->eb.mb.prev_positions[key2];
    int np3 = e->eb.mb.prev_positions[key3];
    if( np2 > min_pos && e->eb.mb.buffer[np2-1] == data[0] )
      {
      pairs[0].dis = e->eb.mb.pos - np2;
      pairs[0].len = maxlen = 2;
      num_pairs = 1;
      }
    if( np2 != np3 && np3 > min_pos && e->eb.mb.buffer[np3-1] == data[0] )
      {
      maxlen = 3;
      np2 = np3;
      pairs[num_pairs].dis = e->eb.mb.pos - np2;
      ++num_pairs;
      }
    if( num_pairs > 0 )
      {
      const int delta = pos1 - np2;
      while( maxlen < len_limit && data[maxlen-delta] == data[maxlen] )
        ++maxlen;
      pairs[num_pairs-1].len = maxlen;
      if( maxlen >= len_limit ) pairs = 0;	/* done. now just skip */
      }
    if( maxlen < 3 ) maxlen = 3;
    }

  e->eb.mb.prev_positions[key2] = pos1;
  e->eb.mb.prev_positions[key3] = pos1;
  newpos1 = e->eb.mb.prev_positions[key4];
  e->eb.mb.prev_positions[key4] = pos1;

  for( count = e->cycles; ; )
    {
    int delta;
    if( newpos1 <= min_pos || --count < 0 ) { *ptr0 = *ptr1 = 0; break; }

    if( e->been_flushed ) len = 0;
    delta = pos1 - newpos1;
    newptr = e->eb.mb.pos_array +
      ( ( e->eb.mb.cyclic_pos - delta +
          ( (e->eb.mb.cyclic_pos >= delta) ? 0 : e->eb.mb.dictionary_size + 1 ) ) << 1 );
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


static void LZe_update_distance_prices( struct LZ_encoder * const e )
  {
  int dis, len_state;
  for( dis = start_dis_model; dis < modeled_distances; ++dis )
    {
    const int dis_slot = dis_slots[dis];
    const int direct_bits = ( dis_slot >> 1 ) - 1;
    const int base = ( 2 | ( dis_slot & 1 ) ) << direct_bits;
    const int price = price_symbol_reversed( e->eb.bm_dis + ( base - dis_slot ),
                                             dis - base, direct_bits );
    for( len_state = 0; len_state < len_states; ++len_state )
      e->dis_prices[len_state][dis] = price;
    }

  for( len_state = 0; len_state < len_states; ++len_state )
    {
    int * const dsp = e->dis_slot_prices[len_state];
    int * const dp = e->dis_prices[len_state];
    const Bit_model * const bmds = e->eb.bm_dis_slot[len_state];
    int slot = 0;
    for( ; slot < end_dis_model; ++slot )
      dsp[slot] = price_symbol6( bmds, slot );
    for( ; slot < e->num_dis_slots; ++slot )
      dsp[slot] = price_symbol6( bmds, slot ) +
                  (((( slot >> 1 ) - 1 ) - dis_align_bits ) << price_shift_bits );

    for( dis = 0; dis < start_dis_model; ++dis )
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
static int LZe_sequence_optimizer( struct LZ_encoder * const e,
                                   const int reps[num_rep_distances],
                                   const State state )
  {
  int main_len, num_pairs, i, rep, num_trials, len;
  int rep_index = 0, cur = 0;
  int replens[num_rep_distances];

  if( e->pending_num_pairs > 0 )		/* from previous call */
    {
    num_pairs = e->pending_num_pairs;
    e->pending_num_pairs = 0;
    }
  else
    num_pairs = LZe_read_match_distances( e );
  main_len = ( num_pairs > 0 ) ? e->pairs[num_pairs-1].len : 0;

  for( i = 0; i < num_rep_distances; ++i )
    {
    replens[i] = Mb_true_match_len( &e->eb.mb, 0, reps[i] + 1 );
    if( replens[i] > replens[rep_index] ) rep_index = i;
    }
  if( replens[rep_index] >= e->match_len_limit )
    {
    e->trials[0].price = replens[rep_index];
    e->trials[0].dis4 = rep_index;
    if( !LZe_move_and_update( e, replens[rep_index] ) ) return 0;
    return replens[rep_index];
    }

  if( main_len >= e->match_len_limit )
    {
    e->trials[0].price = main_len;
    e->trials[0].dis4 = e->pairs[num_pairs-1].dis + num_rep_distances;
    if( !LZe_move_and_update( e, main_len ) ) return 0;
    return main_len;
    }

  {
  const int pos_state = Mb_data_position( &e->eb.mb ) & pos_state_mask;
  const int match_price = price1( e->eb.bm_match[state][pos_state] );
  const int rep_match_price = match_price + price1( e->eb.bm_rep[state] );
  const uint8_t prev_byte = Mb_peek( &e->eb.mb, 1 );
  const uint8_t cur_byte = Mb_peek( &e->eb.mb, 0 );
  const uint8_t match_byte = Mb_peek( &e->eb.mb, reps[0] + 1 );

  e->trials[1].price = price0( e->eb.bm_match[state][pos_state] );
  if( St_is_char( state ) )
    e->trials[1].price += LZeb_price_literal( &e->eb, prev_byte, cur_byte );
  else
    e->trials[1].price += LZeb_price_matched( &e->eb, prev_byte, cur_byte, match_byte );
  e->trials[1].dis4 = -1;				/* literal */

  if( match_byte == cur_byte )
    Tr_update( &e->trials[1], rep_match_price +
               LZeb_price_shortrep( &e->eb, state, pos_state ), 0, 0 );

  num_trials = max( main_len, replens[rep_index] );

  if( num_trials < min_match_len )
    {
    e->trials[0].price = 1;
    e->trials[0].dis4 = e->trials[1].dis4;
    if( !Mb_move_pos( &e->eb.mb ) ) return 0;
    return 1;
    }

  e->trials[0].state = state;
  for( i = 0; i < num_rep_distances; ++i )
    e->trials[0].reps[i] = reps[i];

  for( len = min_match_len; len <= num_trials; ++len )
    e->trials[len].price = infinite_price;

  for( rep = 0; rep < num_rep_distances; ++rep )
    {
    int price;
    if( replens[rep] < min_match_len ) continue;
    price = rep_match_price + LZeb_price_rep( &e->eb, rep, state, pos_state );
    for( len = min_match_len; len <= replens[rep]; ++len )
      Tr_update( &e->trials[len], price +
                 Lp_price( &e->rep_len_prices, len, pos_state ), rep, 0 );
    }

  if( main_len > replens[0] )
    {
    const int normal_match_price = match_price + price0( e->eb.bm_rep[state] );
    int i = 0, len = max( replens[0] + 1, min_match_len );
    while( len > e->pairs[i].len ) ++i;
    while( true )
      {
      const int dis = e->pairs[i].dis;
      Tr_update( &e->trials[len], normal_match_price +
                 LZe_price_pair( e, dis, len, pos_state ),
                 dis + num_rep_distances, 0 );
      if( ++len > e->pairs[i].len && ++i >= num_pairs ) break;
      }
    }
  }

  while( true )				/* price optimization loop */
    {
    struct Trial *cur_trial, *next_trial;
    int newlen, pos_state, triable_bytes, len_limit;
    int start_len = min_match_len;
    int next_price, match_price, rep_match_price;
    State cur_state;
    uint8_t prev_byte, cur_byte, match_byte;

    if( !Mb_move_pos( &e->eb.mb ) ) return 0;
    if( ++cur >= num_trials )		/* no more initialized trials */
      {
      LZe_backward( e, cur );
      return cur;
      }

    num_pairs = LZe_read_match_distances( e );
    newlen = ( num_pairs > 0 ) ? e->pairs[num_pairs-1].len : 0;
    if( newlen >= e->match_len_limit )
      {
      e->pending_num_pairs = num_pairs;
      LZe_backward( e, cur );
      return cur;
      }

    /* give final values to current trial */
    cur_trial = &e->trials[cur];
    {
    const int dis4 = cur_trial->dis4;
    int prev_index = cur_trial->prev_index;
    const int prev_index2 = cur_trial->prev_index2;

    if( prev_index2 == single_step_trial )
      {
      cur_state = e->trials[prev_index].state;
      if( prev_index + 1 == cur )			/* len == 1 */
        {
        if( dis4 == 0 ) cur_state = St_set_short_rep( cur_state );
        else cur_state = St_set_char( cur_state );	/* literal */
        }
      else if( dis4 < num_rep_distances ) cur_state = St_set_rep( cur_state );
      else cur_state = St_set_match( cur_state );
      }
    else
      {
      if( prev_index2 == dual_step_trial )	/* dis4 == 0 (rep0) */
        --prev_index;
      else					/* prev_index2 >= 0 */
        prev_index = prev_index2;
      cur_state = 8;				/* St_set_char_rep(); */
      }
    cur_trial->state = cur_state;
    for( i = 0; i < num_rep_distances; ++i )
      cur_trial->reps[i] = e->trials[prev_index].reps[i];
    mtf_reps( dis4, cur_trial->reps );		/* literal is ignored */
    }

    pos_state = Mb_data_position( &e->eb.mb ) & pos_state_mask;
    prev_byte = Mb_peek( &e->eb.mb, 1 );
    cur_byte = Mb_peek( &e->eb.mb, 0 );
    match_byte = Mb_peek( &e->eb.mb, cur_trial->reps[0] + 1 );

    next_price = cur_trial->price +
                 price0( e->eb.bm_match[cur_state][pos_state] );
    if( St_is_char( cur_state ) )
      next_price += LZeb_price_literal( &e->eb, prev_byte, cur_byte );
    else
      next_price += LZeb_price_matched( &e->eb, prev_byte, cur_byte, match_byte );

    /* try last updates to next trial */
    next_trial = &e->trials[cur+1];

    Tr_update( next_trial, next_price, -1, cur );	/* literal */

    match_price = cur_trial->price + price1( e->eb.bm_match[cur_state][pos_state] );
    rep_match_price = match_price + price1( e->eb.bm_rep[cur_state] );

    if( match_byte == cur_byte && next_trial->dis4 != 0 &&
        next_trial->prev_index2 == single_step_trial )
      {
      const int price = rep_match_price +
                        LZeb_price_shortrep( &e->eb, cur_state, pos_state );
      if( price <= next_trial->price )
        {
        next_trial->price = price;
        next_trial->dis4 = 0;				/* rep0 */
        next_trial->prev_index = cur;
        }
      }

    triable_bytes =
      min( Mb_available_bytes( &e->eb.mb ), max_num_trials - 1 - cur );
    if( triable_bytes < min_match_len ) continue;

    len_limit = min( e->match_len_limit, triable_bytes );

    /* try literal + rep0 */
    if( match_byte != cur_byte && next_trial->prev_index != cur )
      {
      const uint8_t * const data = Mb_ptr_to_current_pos( &e->eb.mb );
      const int dis = cur_trial->reps[0] + 1;
      const int limit = min( e->match_len_limit + 1, triable_bytes );
      int len = 1;
      while( len < limit && data[len-dis] == data[len] ) ++len;
      if( --len >= min_match_len )
        {
        const int pos_state2 = ( pos_state + 1 ) & pos_state_mask;
        const State state2 = St_set_char( cur_state );
        const int price = next_price +
                          price1( e->eb.bm_match[state2][pos_state2] ) +
                          price1( e->eb.bm_rep[state2] ) +
                          LZe_price_rep0_len( e, len, state2, pos_state2 );
        while( num_trials < cur + 1 + len )
          e->trials[++num_trials].price = infinite_price;
        Tr_update2( &e->trials[cur+1+len], price, cur + 1 );
        }
      }

    /* try rep distances */
    for( rep = 0; rep < num_rep_distances; ++rep )
      {
      const uint8_t * const data = Mb_ptr_to_current_pos( &e->eb.mb );
      const int dis = cur_trial->reps[rep] + 1;
      int price;

      if( data[0-dis] != data[0] || data[1-dis] != data[1] ) continue;
      for( len = min_match_len; len < len_limit; ++len )
        if( data[len-dis] != data[len] ) break;
      while( num_trials < cur + len )
        e->trials[++num_trials].price = infinite_price;
      price = rep_match_price + LZeb_price_rep( &e->eb, rep, cur_state, pos_state );
      for( i = min_match_len; i <= len; ++i )
        Tr_update( &e->trials[cur+i], price +
                   Lp_price( &e->rep_len_prices, i, pos_state ), rep, cur );

      if( rep == 0 ) start_len = len + 1;	/* discard shorter matches */

      /* try rep + literal + rep0 */
      {
      int len2 = len + 1;
      const int limit = min( e->match_len_limit + len2, triable_bytes );
      int pos_state2;
      State state2;
      while( len2 < limit && data[len2-dis] == data[len2] ) ++len2;
      len2 -= len + 1;
      if( len2 < min_match_len ) continue;

      pos_state2 = ( pos_state + len ) & pos_state_mask;
      state2 = St_set_rep( cur_state );
      price += Lp_price( &e->rep_len_prices, len, pos_state ) +
               price0( e->eb.bm_match[state2][pos_state2] ) +
               LZeb_price_matched( &e->eb, data[len-1], data[len], data[len-dis] );
      pos_state2 = ( pos_state2 + 1 ) & pos_state_mask;
      state2 = St_set_char( state2 );
      price += price1( e->eb.bm_match[state2][pos_state2] ) +
               price1( e->eb.bm_rep[state2] ) +
               LZe_price_rep0_len( e, len2, state2, pos_state2 );
      while( num_trials < cur + len + 1 + len2 )
        e->trials[++num_trials].price = infinite_price;
      Tr_update3( &e->trials[cur+len+1+len2], price, rep, cur + len + 1, cur );
      }
      }

    /* try matches */
    if( newlen >= start_len && newlen <= len_limit )
      {
      int dis;
      const int normal_match_price = match_price +
                                     price0( e->eb.bm_rep[cur_state] );

      while( num_trials < cur + newlen )
        e->trials[++num_trials].price = infinite_price;

      i = 0;
      while( e->pairs[i].len < start_len ) ++i;
      dis = e->pairs[i].dis;
      for( len = start_len; ; ++len )
        {
        int price = normal_match_price + LZe_price_pair( e, dis, len, pos_state );
        Tr_update( &e->trials[cur+len], price, dis + num_rep_distances, cur );

        /* try match + literal + rep0 */
        if( len == e->pairs[i].len )
          {
          const uint8_t * const data = Mb_ptr_to_current_pos( &e->eb.mb );
          const int dis2 = dis + 1;
          int len2 = len + 1;
          const int limit = min( e->match_len_limit + len2, triable_bytes );
          while( len2 < limit && data[len2-dis2] == data[len2] ) ++len2;
          len2 -= len + 1;
          if( len2 >= min_match_len )
            {
            int pos_state2 = ( pos_state + len ) & pos_state_mask;
            State state2 = St_set_match( cur_state );
            price += price0( e->eb.bm_match[state2][pos_state2] ) +
                     LZeb_price_matched( &e->eb, data[len-1], data[len], data[len-dis2] );
            pos_state2 = ( pos_state2 + 1 ) & pos_state_mask;
            state2 = St_set_char( state2 );
            price += price1( e->eb.bm_match[state2][pos_state2] ) +
                     price1( e->eb.bm_rep[state2] ) +
                     LZe_price_rep0_len( e, len2, state2, pos_state2 );

            while( num_trials < cur + len + 1 + len2 )
              e->trials[++num_trials].price = infinite_price;
            Tr_update3( &e->trials[cur+len+1+len2], price,
                        dis + num_rep_distances, cur + len + 1, cur );
            }
          if( ++i >= num_pairs ) break;
          dis = e->pairs[i].dis;
          }
        }
      }
    }
  }


static bool LZe_encode_member( struct LZ_encoder * const e )
  {
  const bool best = ( e->match_len_limit > 12 );
  const int dis_price_count = best ? 1 : 512;
  const int align_price_count = best ? 1 : dis_align_size;
  const int price_count = ( e->match_len_limit > 36 ) ? 1013 : 4093;
  int ahead, i;
  State * const state = &e->eb.state;

  if( e->eb.member_finished ) return true;
  if( Re_member_position( &e->eb.renc ) >= e->eb.member_size_limit )
    { LZeb_try_full_flush( &e->eb ); return true; }

  if( Mb_data_position( &e->eb.mb ) == 0 &&
      !Mb_data_finished( &e->eb.mb ) )		/* encode first byte */
    {
    const uint8_t prev_byte = 0;
    uint8_t cur_byte;
    if( !Mb_enough_available_bytes( &e->eb.mb ) ||
        !Re_enough_free_bytes( &e->eb.renc ) ) return true;
    cur_byte = Mb_peek( &e->eb.mb, 0 );
    Re_encode_bit( &e->eb.renc, &e->eb.bm_match[*state][0], 0 );
    LZeb_encode_literal( &e->eb, prev_byte, cur_byte );
    CRC32_update_byte( &e->eb.crc, cur_byte );
    LZe_get_match_pairs( e, 0 );
    if( !Mb_move_pos( &e->eb.mb ) ) return false;
    }

  while( !Mb_data_finished( &e->eb.mb ) )
    {
    if( !Mb_enough_available_bytes( &e->eb.mb ) ||
        !Re_enough_free_bytes( &e->eb.renc ) ) return true;
    if( e->price_counter <= 0 && e->pending_num_pairs == 0 )
      {
      e->price_counter = price_count;	/* recalculate prices every these bytes */
      if( e->dis_price_counter <= 0 )
        { e->dis_price_counter = dis_price_count; LZe_update_distance_prices( e ); }
      if( e->align_price_counter <= 0 )
        {
        e->align_price_counter = align_price_count;
        for( i = 0; i < dis_align_size; ++i )
          e->align_prices[i] = price_symbol_reversed( e->eb.bm_align, i, dis_align_bits );
        }
      Lp_update_prices( &e->match_len_prices );
      Lp_update_prices( &e->rep_len_prices );
      }

    ahead = LZe_sequence_optimizer( e, e->eb.reps, *state );
    e->price_counter -= ahead;

    for( i = 0; ahead > 0; )
      {
      const int pos_state =
        ( Mb_data_position( &e->eb.mb ) - ahead ) & pos_state_mask;
      const int len = e->trials[i].price;
      int dis = e->trials[i].dis4;

      bool bit = ( dis < 0 );
      Re_encode_bit( &e->eb.renc, &e->eb.bm_match[*state][pos_state], !bit );
      if( bit )					/* literal byte */
        {
        const uint8_t prev_byte = Mb_peek( &e->eb.mb, ahead + 1 );
        const uint8_t cur_byte = Mb_peek( &e->eb.mb, ahead );
        CRC32_update_byte( &e->eb.crc, cur_byte );
        if( St_is_char( *state ) )
          LZeb_encode_literal( &e->eb, prev_byte, cur_byte );
        else
          {
          const uint8_t match_byte = Mb_peek( &e->eb.mb, ahead + e->eb.reps[0] + 1 );
          LZeb_encode_matched( &e->eb, prev_byte, cur_byte, match_byte );
          }
        *state = St_set_char( *state );
        }
      else					/* match or repeated match */
        {
        CRC32_update_buf( &e->eb.crc, Mb_ptr_to_current_pos( &e->eb.mb ) - ahead, len );
        mtf_reps( dis, e->eb.reps );
        bit = ( dis < num_rep_distances );
        Re_encode_bit( &e->eb.renc, &e->eb.bm_rep[*state], bit );
        if( bit )				/* repeated match */
          {
          bit = ( dis == 0 );
          Re_encode_bit( &e->eb.renc, &e->eb.bm_rep0[*state], !bit );
          if( bit )
            Re_encode_bit( &e->eb.renc, &e->eb.bm_len[*state][pos_state], len > 1 );
          else
            {
            Re_encode_bit( &e->eb.renc, &e->eb.bm_rep1[*state], dis > 1 );
            if( dis > 1 )
              Re_encode_bit( &e->eb.renc, &e->eb.bm_rep2[*state], dis > 2 );
            }
          if( len == 1 ) *state = St_set_short_rep( *state );
          else
            {
            Re_encode_len( &e->eb.renc, &e->eb.rep_len_model, len, pos_state );
            Lp_decrement_counter( &e->rep_len_prices, pos_state );
            *state = St_set_rep( *state );
            }
          }
        else					/* match */
          {
          dis -= num_rep_distances;
          LZeb_encode_pair( &e->eb, dis, len, pos_state );
          if( dis >= modeled_distances ) --e->align_price_counter;
          --e->dis_price_counter;
          Lp_decrement_counter( &e->match_len_prices, pos_state );
          *state = St_set_match( *state );
          }
        }
      ahead -= len; i += len;
      if( Re_member_position( &e->eb.renc ) >= e->eb.member_size_limit )
        {
        if( !Mb_dec_pos( &e->eb.mb, ahead ) ) return false;
        LZeb_try_full_flush( &e->eb );
        return true;
        }
      }
    }
  LZeb_try_full_flush( &e->eb );
  return true;
  }
