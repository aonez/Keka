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

int FLZe_longest_match_len( struct FLZ_encoder * const fe, int * const distance )
  {
  enum { len_limit = 16 };
  const uint8_t * const data = Mb_ptr_to_current_pos( &fe->eb.mb );
  int32_t * ptr0 = fe->eb.mb.pos_array + fe->eb.mb.cyclic_pos;
  const int pos1 = fe->eb.mb.pos + 1;
  int maxlen = 0, newpos1, count;
  const int available = min( Mb_available_bytes( &fe->eb.mb ), max_match_len );
  if( available < len_limit ) { *ptr0 = 0; return 0; }

  fe->key4 = ( ( fe->key4 << 4 ) ^ data[3] ) & fe->eb.mb.key4_mask;
  newpos1 = fe->eb.mb.prev_positions[fe->key4];
  fe->eb.mb.prev_positions[fe->key4] = pos1;

  for( count = 4; ; )
    {
    int32_t * newptr;
    int delta;
    if( newpos1 <= 0 || --count < 0 ||
        ( delta = pos1 - newpos1 ) > fe->eb.mb.dictionary_size )
      { *ptr0 = 0; break; }
    newptr = fe->eb.mb.pos_array +
      ( fe->eb.mb.cyclic_pos - delta +
        ( ( fe->eb.mb.cyclic_pos >= delta ) ? 0 : fe->eb.mb.dictionary_size + 1 ) );

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


bool FLZe_encode_member( struct FLZ_encoder * const fe )
  {
  int rep = 0, i;
  State * const state = &fe->eb.state;

  if( fe->eb.member_finished ) return true;
  if( Re_member_position( &fe->eb.renc ) >= fe->eb.member_size_limit )
    { LZeb_try_full_flush( &fe->eb ); return true; }

  if( Mb_data_position( &fe->eb.mb ) == 0 &&
      !Mb_data_finished( &fe->eb.mb ) )		/* encode first byte */
    {
    const uint8_t prev_byte = 0;
    uint8_t cur_byte;
    if( !Mb_enough_available_bytes( &fe->eb.mb ) ||
        !Re_enough_free_bytes( &fe->eb.renc ) ) return true;
    cur_byte = Mb_peek( &fe->eb.mb, 0 );
    Re_encode_bit( &fe->eb.renc, &fe->eb.bm_match[*state][0], 0 );
    LZeb_encode_literal( &fe->eb, prev_byte, cur_byte );
    CRC32_update_byte( &fe->eb.crc, cur_byte );
    FLZe_reset_key4( fe );
    if( !FLZe_update_and_move( fe, 1 ) ) return false;
    }

  while( !Mb_data_finished( &fe->eb.mb ) &&
         Re_member_position( &fe->eb.renc ) < fe->eb.member_size_limit )
    {
    int match_distance;
    int main_len, pos_state;
    int len = 0;
    if( !Mb_enough_available_bytes( &fe->eb.mb ) ||
        !Re_enough_free_bytes( &fe->eb.renc ) ) return true;
    main_len = FLZe_longest_match_len( fe, &match_distance );
    pos_state = Mb_data_position( &fe->eb.mb ) & pos_state_mask;

    for( i = 0; i < num_rep_distances; ++i )
      {
      const int tlen = Mb_true_match_len( &fe->eb.mb, 0, fe->eb.reps[i] + 1 );
      if( tlen > len ) { len = tlen; rep = i; }
      }
    if( len > min_match_len && len + 3 > main_len )
      {
      CRC32_update_buf( &fe->eb.crc, Mb_ptr_to_current_pos( &fe->eb.mb ), len );
      Re_encode_bit( &fe->eb.renc, &fe->eb.bm_match[*state][pos_state], 1 );
      Re_encode_bit( &fe->eb.renc, &fe->eb.bm_rep[*state], 1 );
      Re_encode_bit( &fe->eb.renc, &fe->eb.bm_rep0[*state], rep != 0 );
      if( rep == 0 )
        Re_encode_bit( &fe->eb.renc, &fe->eb.bm_len[*state][pos_state], 1 );
      else
        {
        int distance;
        Re_encode_bit( &fe->eb.renc, &fe->eb.bm_rep1[*state], rep > 1 );
        if( rep > 1 )
          Re_encode_bit( &fe->eb.renc, &fe->eb.bm_rep2[*state], rep > 2 );
        distance = fe->eb.reps[rep];
        for( i = rep; i > 0; --i ) fe->eb.reps[i] = fe->eb.reps[i-1];
        fe->eb.reps[0] = distance;
        }
      *state = St_set_rep( *state );
      Re_encode_len( &fe->eb.renc, &fe->eb.rep_len_model, len, pos_state );
      if( !Mb_move_pos( &fe->eb.mb ) ) return false;
      if( !FLZe_update_and_move( fe, len - 1 ) ) return false;
      continue;
      }

    if( main_len > min_match_len )
      {
      CRC32_update_buf( &fe->eb.crc, Mb_ptr_to_current_pos( &fe->eb.mb ), main_len );
      Re_encode_bit( &fe->eb.renc, &fe->eb.bm_match[*state][pos_state], 1 );
      Re_encode_bit( &fe->eb.renc, &fe->eb.bm_rep[*state], 0 );
      *state = St_set_match( *state );
      for( i = num_rep_distances - 1; i > 0; --i ) fe->eb.reps[i] = fe->eb.reps[i-1];
      fe->eb.reps[0] = match_distance;
      LZeb_encode_pair( &fe->eb, match_distance, main_len, pos_state );
      if( !Mb_move_pos( &fe->eb.mb ) ) return false;
      if( !FLZe_update_and_move( fe, main_len - 1 ) ) return false;
      continue;
      }

    {
    const uint8_t prev_byte = Mb_peek( &fe->eb.mb, 1 );
    const uint8_t cur_byte = Mb_peek( &fe->eb.mb, 0 );
    const uint8_t match_byte = Mb_peek( &fe->eb.mb, fe->eb.reps[0] + 1 );
    if( !Mb_move_pos( &fe->eb.mb ) ) return false;
    CRC32_update_byte( &fe->eb.crc, cur_byte );

    if( match_byte == cur_byte )
      {
      const int short_rep_price = price1( fe->eb.bm_match[*state][pos_state] ) +
                                  price1( fe->eb.bm_rep[*state] ) +
                                  price0( fe->eb.bm_rep0[*state] ) +
                                  price0( fe->eb.bm_len[*state][pos_state] );
      int price = price0( fe->eb.bm_match[*state][pos_state] );
      if( St_is_char( *state ) )
        price += LZeb_price_literal( &fe->eb, prev_byte, cur_byte );
      else
        price += LZeb_price_matched( &fe->eb, prev_byte, cur_byte, match_byte );
      if( short_rep_price < price )
        {
        Re_encode_bit( &fe->eb.renc, &fe->eb.bm_match[*state][pos_state], 1 );
        Re_encode_bit( &fe->eb.renc, &fe->eb.bm_rep[*state], 1 );
        Re_encode_bit( &fe->eb.renc, &fe->eb.bm_rep0[*state], 0 );
        Re_encode_bit( &fe->eb.renc, &fe->eb.bm_len[*state][pos_state], 0 );
        *state = St_set_short_rep( *state );
        continue;
        }
      }

    /* literal byte */
    Re_encode_bit( &fe->eb.renc, &fe->eb.bm_match[*state][pos_state], 0 );
    if( St_is_char( *state ) )
      LZeb_encode_literal( &fe->eb, prev_byte, cur_byte );
    else
      LZeb_encode_matched( &fe->eb, prev_byte, cur_byte, match_byte );
    *state = St_set_char( *state );
    }
    }

  LZeb_try_full_flush( &fe->eb );
  return true;
  }
