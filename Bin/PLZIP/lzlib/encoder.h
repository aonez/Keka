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

struct Len_prices
  {
  const struct Len_model * lm;
  int len_symbols;
  int count;
  int prices[pos_states][max_len_symbols];
  int counters[pos_states];			/* may decrement below 0 */
  };

static inline void Lp_update_low_mid_prices( struct Len_prices * const lp,
                                             const int pos_state )
  {
  int * const pps = lp->prices[pos_state];
  int tmp = price0( lp->lm->choice1 );
  int len = 0;
  for( ; len < len_low_symbols && len < lp->len_symbols; ++len )
    pps[len] = tmp + price_symbol3( lp->lm->bm_low[pos_state], len );
  if( len >= lp->len_symbols ) return;
  tmp = price1( lp->lm->choice1 ) + price0( lp->lm->choice2 );
  for( ; len < len_low_symbols + len_mid_symbols && len < lp->len_symbols; ++len )
    pps[len] = tmp +
               price_symbol3( lp->lm->bm_mid[pos_state], len - len_low_symbols );
    }

static inline void Lp_update_high_prices( struct Len_prices * const lp )
  {
  const int tmp = price1( lp->lm->choice1 ) + price1( lp->lm->choice2 );
  int len;
  for( len = len_low_symbols + len_mid_symbols; len < lp->len_symbols; ++len )
    /* using 4 slots per value makes "Lp_price" faster */
    lp->prices[3][len] = lp->prices[2][len] =
    lp->prices[1][len] = lp->prices[0][len] = tmp +
      price_symbol8( lp->lm->bm_high, len - len_low_symbols - len_mid_symbols );
  }

static inline void Lp_reset( struct Len_prices * const lp )
  { int i; for( i = 0; i < pos_states; ++i ) lp->counters[i] = 0; }

static inline void Lp_init( struct Len_prices * const lp,
                            const struct Len_model * const lm,
                            const int match_len_limit )
  {
  lp->lm = lm;
  lp->len_symbols = match_len_limit + 1 - min_match_len;
  lp->count = ( match_len_limit > 12 ) ? 1 : lp->len_symbols;
  Lp_reset( lp );
  }

static inline void Lp_decrement_counter( struct Len_prices * const lp,
                                         const int pos_state )
  { --lp->counters[pos_state]; }

static inline void Lp_update_prices( struct Len_prices * const lp )
  {
  int pos_state;
  bool high_pending = false;
  for( pos_state = 0; pos_state < pos_states; ++pos_state )
    if( lp->counters[pos_state] <= 0 )
      { lp->counters[pos_state] = lp->count;
        Lp_update_low_mid_prices( lp, pos_state ); high_pending = true; }
  if( high_pending && lp->len_symbols > len_low_symbols + len_mid_symbols )
    Lp_update_high_prices( lp );
  }

static inline int Lp_price( const struct Len_prices * const lp,
                            const int len, const int pos_state )
  { return lp->prices[pos_state][len - min_match_len]; }


struct Pair			/* distance-length pair */
  {
  int dis;
  int len;
  };

enum { infinite_price = 0x0FFFFFFF,
       max_num_trials = 1 << 13,
       single_step_trial = -2,
       dual_step_trial = -1 };

struct Trial
  {
  State state;
  int price;		/* dual use var; cumulative price, match length */
  int dis4;		/* -1 for literal, or rep, or match distance + 4 */
  int prev_index;	/* index of prev trial in trials[] */
  int prev_index2;	/*   -2  trial is single step */
			/*   -1  literal + rep0 */
			/* >= 0  ( rep or match ) + literal + rep0 */
  int reps[num_rep_distances];
  };

static inline void Tr_update( struct Trial * const trial, const int pr,
                              const int distance4, const int p_i )
  {
  if( pr < trial->price )
    { trial->price = pr; trial->dis4 = distance4; trial->prev_index = p_i;
      trial->prev_index2 = single_step_trial; }
  }

static inline void Tr_update2( struct Trial * const trial, const int pr,
                               const int p_i )
  {
  if( pr < trial->price )
    { trial->price = pr; trial->dis4 = 0; trial->prev_index = p_i;
      trial->prev_index2 = dual_step_trial; }
  }

static inline void Tr_update3( struct Trial * const trial, const int pr,
                               const int distance4, const int p_i,
                               const int p_i2 )
  {
  if( pr < trial->price )
    { trial->price = pr; trial->dis4 = distance4; trial->prev_index = p_i;
      trial->prev_index2 = p_i2; }
  }


struct LZ_encoder
  {
  struct LZ_encoder_base eb;
  int cycles;
  int match_len_limit;
  struct Len_prices match_len_prices;
  struct Len_prices rep_len_prices;
  int pending_num_pairs;
  struct Pair pairs[max_match_len+1];
  struct Trial trials[max_num_trials];

  int dis_slot_prices[len_states][2*max_dictionary_bits];
  int dis_prices[len_states][modeled_distances];
  int align_prices[dis_align_size];
  int num_dis_slots;
  int price_counter;		/* counters may decrement below 0 */
  int dis_price_counter;
  int align_price_counter;
  bool been_flushed;
  };

static inline bool Mb_dec_pos( struct Matchfinder_base * const mb,
                               const int ahead )
  {
  if( ahead < 0 || mb->pos < ahead ) return false;
  mb->pos -= ahead;
  if( mb->cyclic_pos < ahead ) mb->cyclic_pos += mb->dictionary_size + 1;
  mb->cyclic_pos -= ahead;
  return true;
  }

static int LZe_get_match_pairs( struct LZ_encoder * const e, struct Pair * pairs );

       /* move-to-front dis in/into reps; do nothing if( dis4 <= 0 ) */
static inline void mtf_reps( const int dis4, int reps[num_rep_distances] )
  {
  if( dis4 >= num_rep_distances )			/* match */
    {
    reps[3] = reps[2]; reps[2] = reps[1]; reps[1] = reps[0];
    reps[0] = dis4 - num_rep_distances;
    }
  else if( dis4 > 0 )				/* repeated match */
    {
    const int distance = reps[dis4];
    int i; for( i = dis4; i > 0; --i ) reps[i] = reps[i-1];
    reps[0] = distance;
    }
  }

static inline int LZeb_price_shortrep( const struct LZ_encoder_base * const eb,
                                       const State state, const int pos_state )
  {
  return price0( eb->bm_rep0[state] ) + price0( eb->bm_len[state][pos_state] );
  }

static inline int LZeb_price_rep( const struct LZ_encoder_base * const eb,
                                  const int rep, const State state,
                                  const int pos_state )
  {
  int price;
  if( rep == 0 ) return price0( eb->bm_rep0[state] ) +
                        price1( eb->bm_len[state][pos_state] );
  price = price1( eb->bm_rep0[state] );
  if( rep == 1 )
    price += price0( eb->bm_rep1[state] );
  else
    {
    price += price1( eb->bm_rep1[state] );
    price += price_bit( eb->bm_rep2[state], rep - 2 );
    }
  return price;
  }

static inline int LZe_price_rep0_len( const struct LZ_encoder * const e,
                                      const int len, const State state,
                                      const int pos_state )
  {
  return LZeb_price_rep( &e->eb, 0, state, pos_state ) +
         Lp_price( &e->rep_len_prices, len, pos_state );
  }

static inline int LZe_price_pair( const struct LZ_encoder * const e,
                                  const int dis, const int len,
                                  const int pos_state )
  {
  const int price = Lp_price( &e->match_len_prices, len, pos_state );
  const int len_state = get_len_state( len );
  if( dis < modeled_distances )
    return price + e->dis_prices[len_state][dis];
  else
    return price + e->dis_slot_prices[len_state][get_slot( dis )] +
           e->align_prices[dis & (dis_align_size - 1)];
  }

static inline int LZe_read_match_distances( struct LZ_encoder * const e )
  {
  const int num_pairs = LZe_get_match_pairs( e, e->pairs );
  if( num_pairs > 0 )
    {
    const int len = e->pairs[num_pairs-1].len;
    if( len == e->match_len_limit && len < max_match_len )
      e->pairs[num_pairs-1].len =
        Mb_true_match_len( &e->eb.mb, len, e->pairs[num_pairs-1].dis + 1 );
    }
  return num_pairs;
  }

static inline bool LZe_move_and_update( struct LZ_encoder * const e, int n )
  {
  while( true )
    {
    if( !Mb_move_pos( &e->eb.mb ) ) return false;
    if( --n <= 0 ) break;
    LZe_get_match_pairs( e, 0 );
    }
  return true;
  }

static inline void LZe_backward( struct LZ_encoder * const e, int cur )
  {
  int dis4 = e->trials[cur].dis4;
  while( cur > 0 )
    {
    const int prev_index = e->trials[cur].prev_index;
    struct Trial * const prev_trial = &e->trials[prev_index];

    if( e->trials[cur].prev_index2 != single_step_trial )
      {
      prev_trial->dis4 = -1;					/* literal */
      prev_trial->prev_index = prev_index - 1;
      prev_trial->prev_index2 = single_step_trial;
      if( e->trials[cur].prev_index2 >= 0 )
        {
        struct Trial * const prev_trial2 = &e->trials[prev_index-1];
        prev_trial2->dis4 = dis4; dis4 = 0;			/* rep0 */
        prev_trial2->prev_index = e->trials[cur].prev_index2;
        prev_trial2->prev_index2 = single_step_trial;
        }
      }
    prev_trial->price = cur - prev_index;			/* len */
    cur = dis4; dis4 = prev_trial->dis4; prev_trial->dis4 = cur;
    cur = prev_index;
    }
  }

enum { num_prev_positions3 = 1 << 16,
       num_prev_positions2 = 1 << 10 };

static inline bool LZe_init( struct LZ_encoder * const e,
                             const int dict_size, const int len_limit,
                             const unsigned long long member_size )
  {
  enum { before = max_num_trials,
         /* bytes to keep in buffer after pos */
         after_size = max_num_trials + ( 2 * max_match_len ) + 1,
         dict_factor = 2,
         num_prev_positions23 = num_prev_positions2 + num_prev_positions3,
         pos_array_factor = 2,
         min_free_bytes = 2 * max_num_trials };

  if( !LZeb_init( &e->eb, before, dict_size, after_size, dict_factor,
                  num_prev_positions23, pos_array_factor, min_free_bytes,
                  member_size ) ) return false;
  e->cycles = ( len_limit < max_match_len ) ? 16 + ( len_limit / 2 ) : 256;
  e->match_len_limit = len_limit;
  Lp_init( &e->match_len_prices, &e->eb.match_len_model, e->match_len_limit );
  Lp_init( &e->rep_len_prices, &e->eb.rep_len_model, e->match_len_limit );
  e->pending_num_pairs = 0;
  e->num_dis_slots = 2 * real_bits( e->eb.mb.dictionary_size - 1 );
  e->trials[1].prev_index = 0;
  e->trials[1].prev_index2 = single_step_trial;
  e->price_counter = 0;
  e->dis_price_counter = 0;
  e->align_price_counter = 0;
  e->been_flushed = false;
  return true;
  }

static inline void LZe_reset( struct LZ_encoder * const e,
                              const unsigned long long member_size )
  {
  LZeb_reset( &e->eb, member_size );
  Lp_reset( &e->match_len_prices );
  Lp_reset( &e->rep_len_prices );
  e->pending_num_pairs = 0;
  e->price_counter = 0;
  e->dis_price_counter = 0;
  e->align_price_counter = 0;
  e->been_flushed = false;
  }
