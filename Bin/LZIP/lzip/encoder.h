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

class Len_prices
  {
  const Len_model & lm;
  const int len_symbols;
  const int count;
  int prices[pos_states][max_len_symbols];
  int counters[pos_states];			// may decrement below 0

  void update_low_mid_prices( const int pos_state )
    {
    int * const pps = prices[pos_state];
    int tmp = price0( lm.choice1 );
    int len = 0;
    for( ; len < len_low_symbols && len < len_symbols; ++len )
      pps[len] = tmp + price_symbol3( lm.bm_low[pos_state], len );
    if( len >= len_symbols ) return;
    tmp = price1( lm.choice1 ) + price0( lm.choice2 );
    for( ; len < len_low_symbols + len_mid_symbols && len < len_symbols; ++len )
      pps[len] = tmp +
                 price_symbol3( lm.bm_mid[pos_state], len - len_low_symbols );
    }

  void update_high_prices()
    {
    const int tmp = price1( lm.choice1 ) + price1( lm.choice2 );
    for( int len = len_low_symbols + len_mid_symbols; len < len_symbols; ++len )
      // using 4 slots per value makes "price" faster
      prices[3][len] = prices[2][len] = prices[1][len] = prices[0][len] = tmp +
        price_symbol8( lm.bm_high, len - len_low_symbols - len_mid_symbols );
    }

public:
  void reset() { for( int i = 0; i < pos_states; ++i ) counters[i] = 0; }

  Len_prices( const Len_model & m, const int match_len_limit )
    :
    lm( m ),
    len_symbols( match_len_limit + 1 - min_match_len ),
    count( ( match_len_limit > 12 ) ? 1 : len_symbols )
    { reset(); }

  void decrement_counter( const int pos_state ) { --counters[pos_state]; }

  void update_prices()
    {
    bool high_pending = false;
    for( int pos_state = 0; pos_state < pos_states; ++pos_state )
      if( counters[pos_state] <= 0 )
        { counters[pos_state] = count;
          update_low_mid_prices( pos_state ); high_pending = true; }
    if( high_pending && len_symbols > len_low_symbols + len_mid_symbols )
      update_high_prices();
    }

  int price( const int len, const int pos_state ) const
    { return prices[pos_state][len - min_match_len]; }
  };


class LZ_encoder : public LZ_encoder_base
  {
  struct Pair			// distance-length pair
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
    int price;		// dual use var; cumulative price, match length
    int dis4;		// -1 for literal, or rep, or match distance + 4
    int prev_index;	// index of prev trial in trials[]
    int prev_index2;	//   -2  trial is single step
			//   -1  literal + rep0
			// >= 0  ( rep or match ) + literal + rep0
    int reps[num_rep_distances];

    void update( const int pr, const int distance4, const int p_i )
      {
      if( pr < price )
        { price = pr; dis4 = distance4; prev_index = p_i;
          prev_index2 = single_step_trial; }
      }

    void update2( const int pr, const int p_i )
      {
      if( pr < price )
        { price = pr; dis4 = 0; prev_index = p_i;
          prev_index2 = dual_step_trial; }
      }

    void update3( const int pr, const int distance4, const int p_i,
                  const int p_i2 )
      {
      if( pr < price )
        { price = pr; dis4 = distance4; prev_index = p_i;
          prev_index2 = p_i2; }
      }
    };

  const int cycles;
  const int match_len_limit;
  Len_prices match_len_prices;
  Len_prices rep_len_prices;
  int pending_num_pairs;
  Pair pairs[max_match_len+1];
  Trial trials[max_num_trials];

  int dis_slot_prices[len_states][2*max_dictionary_bits];
  int dis_prices[len_states][modeled_distances];
  int align_prices[dis_align_size];
  const int num_dis_slots;

  bool dec_pos( const int ahead )
    {
    if( ahead < 0 || pos < ahead ) return false;
    pos -= ahead;
    if( cyclic_pos < ahead ) cyclic_pos += dictionary_size + 1;
    cyclic_pos -= ahead;
    return true;
    }

  int get_match_pairs( Pair * pairs = 0 );
  void update_distance_prices();

       // move-to-front dis in/into reps; do nothing if( dis4 <= 0 )
  static void mtf_reps( const int dis4, int reps[num_rep_distances] )
    {
    if( dis4 >= num_rep_distances )			// match
      {
      reps[3] = reps[2]; reps[2] = reps[1]; reps[1] = reps[0];
      reps[0] = dis4 - num_rep_distances;
      }
    else if( dis4 > 0 )				// repeated match
      {
      const int distance = reps[dis4];
      for( int i = dis4; i > 0; --i ) reps[i] = reps[i-1];
      reps[0] = distance;
      }
    }

  int price_shortrep( const State state, const int pos_state ) const
    {
    return price0( bm_rep0[state()] ) + price0( bm_len[state()][pos_state] );
    }

  int price_rep( const int rep, const State state, const int pos_state ) const
    {
    if( rep == 0 ) return price0( bm_rep0[state()] ) +
                          price1( bm_len[state()][pos_state] );
    int price = price1( bm_rep0[state()] );
    if( rep == 1 )
      price += price0( bm_rep1[state()] );
    else
      {
      price += price1( bm_rep1[state()] );
      price += price_bit( bm_rep2[state()], rep - 2 );
      }
    return price;
    }

  int price_rep0_len( const int len, const State state, const int pos_state ) const
    {
    return price_rep( 0, state, pos_state ) +
           rep_len_prices.price( len, pos_state );
    }

  int price_pair( const int dis, const int len, const int pos_state ) const
    {
    const int price = match_len_prices.price( len, pos_state );
    const int len_state = get_len_state( len );
    if( dis < modeled_distances )
      return price + dis_prices[len_state][dis];
    else
      return price + dis_slot_prices[len_state][get_slot( dis )] +
             align_prices[dis & (dis_align_size - 1)];
    }

  int read_match_distances()
    {
    const int num_pairs = get_match_pairs( pairs );
    if( num_pairs > 0 )
      {
      const int len = pairs[num_pairs-1].len;
      if( len == match_len_limit && len < max_match_len )
        pairs[num_pairs-1].len =
          true_match_len( len, pairs[num_pairs-1].dis + 1 );
      }
    return num_pairs;
    }

  void move_and_update( int n )
    {
    while( true )
      {
      move_pos();
      if( --n <= 0 ) break;
      get_match_pairs();
      }
    }

  void backward( int cur )
    {
    int dis4 = trials[cur].dis4;
    while( cur > 0 )
      {
      const int prev_index = trials[cur].prev_index;
      Trial & prev_trial = trials[prev_index];

      if( trials[cur].prev_index2 != single_step_trial )
        {
        prev_trial.dis4 = -1;					// literal
        prev_trial.prev_index = prev_index - 1;
        prev_trial.prev_index2 = single_step_trial;
        if( trials[cur].prev_index2 >= 0 )
          {
          Trial & prev_trial2 = trials[prev_index-1];
          prev_trial2.dis4 = dis4; dis4 = 0;			// rep0
          prev_trial2.prev_index = trials[cur].prev_index2;
          prev_trial2.prev_index2 = single_step_trial;
          }
        }
      prev_trial.price = cur - prev_index;			// len
      cur = dis4; dis4 = prev_trial.dis4; prev_trial.dis4 = cur;
      cur = prev_index;
      }
    }

  int sequence_optimizer( const int reps[num_rep_distances],
                          const State state );

  enum { before = max_num_trials,
         // bytes to keep in buffer after pos
         after_size = ( 2 * max_match_len ) + 1,
         dict_factor = 2,
         num_prev_positions3 = 1 << 16,
         num_prev_positions2 = 1 << 10,
         num_prev_positions23 = num_prev_positions2 + num_prev_positions3,
         pos_array_factor = 2 };

public:
  LZ_encoder( const int dict_size, const int len_limit,
              const int ifd, const int outfd )
    :
    LZ_encoder_base( before, dict_size, after_size, dict_factor,
                     num_prev_positions23, pos_array_factor, ifd, outfd ),
    cycles( ( len_limit < max_match_len ) ? 16 + ( len_limit / 2 ) : 256 ),
    match_len_limit( len_limit ),
    match_len_prices( match_len_model, match_len_limit ),
    rep_len_prices( rep_len_model, match_len_limit ),
    pending_num_pairs( 0 ),
    num_dis_slots( 2 * real_bits( dictionary_size - 1 ) )
    {
    trials[1].prev_index = 0;
    trials[1].prev_index2 = single_step_trial;
    }

  void reset()
    {
    LZ_encoder_base::reset();
    match_len_prices.reset();
    rep_len_prices.reset();
    pending_num_pairs = 0;
    }

  bool encode_member( const unsigned long long member_size );
  };
