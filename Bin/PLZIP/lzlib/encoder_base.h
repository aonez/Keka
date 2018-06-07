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

enum { price_shift_bits = 6,
       price_step_bits = 2 };

static const uint8_t dis_slots[1<<10] =
  {
   0,  1,  2,  3,  4,  4,  5,  5,  6,  6,  6,  6,  7,  7,  7,  7,
   8,  8,  8,  8,  8,  8,  8,  8,  9,  9,  9,  9,  9,  9,  9,  9,
  10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
  11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
  12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
  12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
  13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
  13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
  14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
  14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
  14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
  14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
  15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
  16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
  16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
  16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
  16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
  16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
  16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
  16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
  16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
  17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
  17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
  17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
  17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
  17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
  17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
  17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
  17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
  18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
  18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
  18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
  18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
  18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
  18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
  18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
  18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
  18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
  18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
  18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
  18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
  18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
  18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
  18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
  18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
  19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
  19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
  19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
  19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
  19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
  19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
  19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
  19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
  19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
  19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
  19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
  19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
  19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
  19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
  19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
  19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19 };

static inline uint8_t get_slot( const unsigned dis )
  {
  if( dis < (1 << 10) ) return dis_slots[dis];
  if( dis < (1 << 19) ) return dis_slots[dis>> 9] + 18;
  if( dis < (1 << 28) ) return dis_slots[dis>>18] + 36;
  return dis_slots[dis>>27] + 54;
  }


static const short prob_prices[bit_model_total >> price_step_bits] =
{
640, 539, 492, 461, 438, 419, 404, 390, 379, 369, 359, 351, 343, 336, 330, 323,
318, 312, 307, 302, 298, 293, 289, 285, 281, 277, 274, 270, 267, 264, 261, 258,
255, 252, 250, 247, 244, 242, 239, 237, 235, 232, 230, 228, 226, 224, 222, 220,
218, 216, 214, 213, 211, 209, 207, 206, 204, 202, 201, 199, 198, 196, 195, 193,
192, 190, 189, 188, 186, 185, 184, 182, 181, 180, 178, 177, 176, 175, 174, 172,
171, 170, 169, 168, 167, 166, 165, 164, 163, 162, 161, 159, 158, 157, 157, 156,
155, 154, 153, 152, 151, 150, 149, 148, 147, 146, 145, 145, 144, 143, 142, 141,
140, 140, 139, 138, 137, 136, 136, 135, 134, 133, 133, 132, 131, 130, 130, 129,
128, 127, 127, 126, 125, 125, 124, 123, 123, 122, 121, 121, 120, 119, 119, 118,
117, 117, 116, 115, 115, 114, 114, 113, 112, 112, 111, 111, 110, 109, 109, 108,
108, 107, 106, 106, 105, 105, 104, 104, 103, 103, 102, 101, 101, 100, 100,  99,
 99,  98,  98,  97,  97,  96,  96,  95,  95,  94,  94,  93,  93,  92,  92,  91,
 91,  90,  90,  89,  89,  88,  88,  88,  87,  87,  86,  86,  85,  85,  84,  84,
 83,  83,  83,  82,  82,  81,  81,  80,  80,  80,  79,  79,  78,  78,  77,  77,
 77,  76,  76,  75,  75,  75,  74,  74,  73,  73,  73,  72,  72,  71,  71,  71,
 70,  70,  70,  69,  69,  68,  68,  68,  67,  67,  67,  66,  66,  65,  65,  65,
 64,  64,  64,  63,  63,  63,  62,  62,  61,  61,  61,  60,  60,  60,  59,  59,
 59,  58,  58,  58,  57,  57,  57,  56,  56,  56,  55,  55,  55,  54,  54,  54,
 53,  53,  53,  53,  52,  52,  52,  51,  51,  51,  50,  50,  50,  49,  49,  49,
 48,  48,  48,  48,  47,  47,  47,  46,  46,  46,  45,  45,  45,  45,  44,  44,
 44,  43,  43,  43,  43,  42,  42,  42,  41,  41,  41,  41,  40,  40,  40,  40,
 39,  39,  39,  38,  38,  38,  38,  37,  37,  37,  37,  36,  36,  36,  35,  35,
 35,  35,  34,  34,  34,  34,  33,  33,  33,  33,  32,  32,  32,  32,  31,  31,
 31,  31,  30,  30,  30,  30,  29,  29,  29,  29,  28,  28,  28,  28,  27,  27,
 27,  27,  26,  26,  26,  26,  26,  25,  25,  25,  25,  24,  24,  24,  24,  23,
 23,  23,  23,  22,  22,  22,  22,  22,  21,  21,  21,  21,  20,  20,  20,  20,
 20,  19,  19,  19,  19,  18,  18,  18,  18,  18,  17,  17,  17,  17,  17,  16,
 16,  16,  16,  15,  15,  15,  15,  15,  14,  14,  14,  14,  14,  13,  13,  13,
 13,  13,  12,  12,  12,  12,  12,  11,  11,  11,  11,  10,  10,  10,  10,  10,
  9,   9,   9,   9,   9,   9,   8,   8,   8,   8,   8,   7,   7,   7,   7,   7,
  6,   6,   6,   6,   6,   5,   5,   5,   5,   5,   4,   4,   4,   4,   4,   4,
  3,   3,   3,   3,   3,   2,   2,   2,   2,   2,   1,   1,   1,   1,   1,   1 };

static inline int get_price( const int probability )
  { return prob_prices[probability >> price_step_bits]; }


static inline int price0( const Bit_model probability )
  { return get_price( probability ); }

static inline int price1( const Bit_model probability )
  { return get_price( bit_model_total - probability ); }

static inline int price_bit( const Bit_model bm, const bool bit )
  { return ( bit ? price1( bm ) : price0( bm ) ); }


static inline int price_symbol3( const Bit_model bm[], int symbol )
  {
  int price;
  bool bit = symbol & 1;
  symbol |= 8; symbol >>= 1;
  price = price_bit( bm[symbol], bit );
  bit = symbol & 1; symbol >>= 1; price += price_bit( bm[symbol], bit );
  return price + price_bit( bm[1], symbol & 1 );
  }


static inline int price_symbol6( const Bit_model bm[], unsigned symbol )
  {
  int price;
  bool bit = symbol & 1;
  symbol |= 64; symbol >>= 1;
  price = price_bit( bm[symbol], bit );
  bit = symbol & 1; symbol >>= 1; price += price_bit( bm[symbol], bit );
  bit = symbol & 1; symbol >>= 1; price += price_bit( bm[symbol], bit );
  bit = symbol & 1; symbol >>= 1; price += price_bit( bm[symbol], bit );
  bit = symbol & 1; symbol >>= 1; price += price_bit( bm[symbol], bit );
  return price + price_bit( bm[1], symbol & 1 );
  }


static inline int price_symbol8( const Bit_model bm[], int symbol )
  {
  int price;
  bool bit = symbol & 1;
  symbol |= 0x100; symbol >>= 1;
  price = price_bit( bm[symbol], bit );
  bit = symbol & 1; symbol >>= 1; price += price_bit( bm[symbol], bit );
  bit = symbol & 1; symbol >>= 1; price += price_bit( bm[symbol], bit );
  bit = symbol & 1; symbol >>= 1; price += price_bit( bm[symbol], bit );
  bit = symbol & 1; symbol >>= 1; price += price_bit( bm[symbol], bit );
  bit = symbol & 1; symbol >>= 1; price += price_bit( bm[symbol], bit );
  bit = symbol & 1; symbol >>= 1; price += price_bit( bm[symbol], bit );
  return price + price_bit( bm[1], symbol & 1 );
  }


static inline int price_symbol_reversed( const Bit_model bm[], int symbol,
                                         const int num_bits )
  {
  int price = 0;
  int model = 1;
  int i;
  for( i = num_bits; i > 0; --i )
    {
    const bool bit = symbol & 1;
    symbol >>= 1;
    price += price_bit( bm[model], bit );
    model = ( model << 1 ) | bit;
    }
  return price;
  }


static inline int price_matched( const Bit_model bm[], unsigned symbol,
                                 unsigned match_byte )
  {
  int price = 0;
  unsigned mask = 0x100;
  symbol |= mask;
  while( true )
    {
    const unsigned match_bit = ( match_byte <<= 1 ) & mask;
    const bool bit = ( symbol <<= 1 ) & 0x100;
    price += price_bit( bm[(symbol>>9)+match_bit+mask], bit );
    if( symbol >= 0x10000 ) return price;
    mask &= ~(match_bit ^ symbol);	/* if( match_bit != bit ) mask = 0; */
    }
  }


struct Matchfinder_base
  {
  unsigned long long partial_data_pos;
  uint8_t * buffer;		/* input buffer */
  int32_t * prev_positions;	/* 1 + last seen position of key. else 0 */
  int32_t * pos_array;		/* may be tree or chain */
  int before_size;		/* bytes to keep in buffer before dictionary */
  int after_size;		/* bytes to keep in buffer after pos */
  int buffer_size;
  int dictionary_size;		/* bytes to keep in buffer before pos */
  int pos;			/* current pos in buffer */
  int cyclic_pos;		/* cycles through [0, dictionary_size] */
  int stream_pos;		/* first byte not yet read from file */
  int pos_limit;		/* when reached, a new block must be read */
  int key4_mask;
  int num_prev_positions23;
  int num_prev_positions;	/* size of prev_positions */
  int pos_array_size;
  bool at_stream_end;		/* stream_pos shows real end of file */
  bool flushing;
  };

static bool Mb_normalize_pos( struct Matchfinder_base * const mb );

static bool Mb_init( struct Matchfinder_base * const mb,
                     const int before, const int dict_size,
                     const int after_size, const int dict_factor,
                     const int num_prev_positions23,
                     const int pos_array_factor );

static inline void Mb_free( struct Matchfinder_base * const mb )
  { free( mb->prev_positions ); free( mb->buffer ); }

static inline uint8_t Mb_peek( const struct Matchfinder_base * const mb,
                               const int distance )
  { return mb->buffer[mb->pos-distance]; }

static inline int Mb_available_bytes( const struct Matchfinder_base * const mb )
  { return mb->stream_pos - mb->pos; }

static inline unsigned long long
Mb_data_position( const struct Matchfinder_base * const mb )
  { return mb->partial_data_pos + mb->pos; }

static inline void Mb_finish( struct Matchfinder_base * const mb )
  { mb->at_stream_end = true; mb->flushing = false; }

static inline bool Mb_data_finished( const struct Matchfinder_base * const mb )
  { return mb->at_stream_end && !mb->flushing && mb->pos >= mb->stream_pos; }

static inline bool Mb_flushing_or_end( const struct Matchfinder_base * const mb )
  { return mb->at_stream_end || mb->flushing; }

static inline int Mb_free_bytes( const struct Matchfinder_base * const mb )
  { if( Mb_flushing_or_end( mb ) ) return 0;
    return mb->buffer_size - mb->stream_pos; }

static inline bool Mb_enough_available_bytes( const struct Matchfinder_base * const mb )
  { return ( mb->pos + mb->after_size <= mb->stream_pos ||
             ( Mb_flushing_or_end( mb ) && mb->pos < mb->stream_pos ) ); }

static inline const uint8_t *
Mb_ptr_to_current_pos( const struct Matchfinder_base * const mb )
  { return mb->buffer + mb->pos; }

static int Mb_write_data( struct Matchfinder_base * const mb,
                          const uint8_t * const inbuf, const int size )
  {
  const int sz = min( mb->buffer_size - mb->stream_pos, size );
  if( Mb_flushing_or_end( mb ) || sz <= 0 ) return 0;
  memcpy( mb->buffer + mb->stream_pos, inbuf, sz );
  mb->stream_pos += sz;
  return sz;
  }

static inline int Mb_true_match_len( const struct Matchfinder_base * const mb,
                                     const int index, const int distance )
  {
  const uint8_t * const data = mb->buffer + mb->pos;
  int i = index;
  const int len_limit = min( Mb_available_bytes( mb ), max_match_len );
  while( i < len_limit && data[i-distance] == data[i] ) ++i;
  return i;
  }

static inline bool Mb_move_pos( struct Matchfinder_base * const mb )
  {
  if( ++mb->cyclic_pos > mb->dictionary_size ) mb->cyclic_pos = 0;
  if( ++mb->pos >= mb->pos_limit ) return Mb_normalize_pos( mb );
  return true;
  }


struct Range_encoder
  {
  struct Circular_buffer cb;
  unsigned min_free_bytes;
  uint64_t low;
  unsigned long long partial_member_pos;
  uint32_t range;
  unsigned ff_count;
  uint8_t cache;
  File_header header;
  };

static inline void Re_shift_low( struct Range_encoder * const renc )
  {
  if( renc->low >> 24 != 0xFF )
    {
    const bool carry = ( renc->low > 0xFFFFFFFFU );
    Cb_put_byte( &renc->cb, renc->cache + carry );
    for( ; renc->ff_count > 0; --renc->ff_count )
      Cb_put_byte( &renc->cb, 0xFF + carry );
    renc->cache = renc->low >> 24;
    }
  else ++renc->ff_count;
  renc->low = ( renc->low & 0x00FFFFFFU ) << 8;
  }

static inline void Re_reset( struct Range_encoder * const renc )
  {
  int i;
  Cb_reset( &renc->cb );
  renc->low = 0;
  renc->partial_member_pos = 0;
  renc->range = 0xFFFFFFFFU;
  renc->ff_count = 0;
  renc->cache = 0;
  for( i = 0; i < Fh_size; ++i )
    Cb_put_byte( &renc->cb, renc->header[i] );
  }

static inline bool Re_init( struct Range_encoder * const renc,
                            const unsigned dictionary_size,
                            const unsigned min_free_bytes )
  {
  if( !Cb_init( &renc->cb, 65536 + min_free_bytes ) ) return false;
  renc->min_free_bytes = min_free_bytes;
  Fh_set_magic( renc->header );
  Fh_set_dictionary_size( renc->header, dictionary_size );
  Re_reset( renc );
  return true;
  }

static inline void Re_free( struct Range_encoder * const renc )
  { Cb_free( &renc->cb ); }

static inline unsigned long long
Re_member_position( const struct Range_encoder * const renc )
  { return renc->partial_member_pos + Cb_used_bytes( &renc->cb ) + renc->ff_count; }

static inline bool Re_enough_free_bytes( const struct Range_encoder * const renc )
  { return Cb_free_bytes( &renc->cb ) >= renc->min_free_bytes + renc->ff_count; }

static inline int Re_read_data( struct Range_encoder * const renc,
                                uint8_t * const out_buffer, const int out_size )
  {
  const int size = Cb_read_data( &renc->cb, out_buffer, out_size );
  if( size > 0 ) renc->partial_member_pos += size;
  return size;
  }

static inline void Re_flush( struct Range_encoder * const renc )
  {
  int i; for( i = 0; i < 5; ++i ) Re_shift_low( renc );
  renc->low = 0;
  renc->range = 0xFFFFFFFFU;
  renc->ff_count = 0;
  renc->cache = 0;
  }

static inline void Re_encode( struct Range_encoder * const renc,
                              const int symbol, const int num_bits )
  {
  unsigned mask;
  for( mask = 1 << ( num_bits - 1 ); mask > 0; mask >>= 1 )
    {
    renc->range >>= 1;
    if( symbol & mask ) renc->low += renc->range;
    if( renc->range <= 0x00FFFFFFU )
      { renc->range <<= 8; Re_shift_low( renc ); }
    }
  }

static inline void Re_encode_bit( struct Range_encoder * const renc,
                                  Bit_model * const probability, const bool bit )
  {
  const uint32_t bound = ( renc->range >> bit_model_total_bits ) * *probability;
  if( !bit )
    {
    renc->range = bound;
    *probability += (bit_model_total - *probability) >> bit_model_move_bits;
    }
  else
    {
    renc->low += bound;
    renc->range -= bound;
    *probability -= *probability >> bit_model_move_bits;
    }
  if( renc->range <= 0x00FFFFFFU ) { renc->range <<= 8; Re_shift_low( renc ); }
  }

static inline void Re_encode_tree3( struct Range_encoder * const renc,
                                    Bit_model bm[], const int symbol )
  {
  int model = 1;
  bool bit = ( symbol >> 2 ) & 1;
  Re_encode_bit( renc, &bm[model], bit ); model = ( model << 1 ) | bit;
  bit = ( symbol >> 1 ) & 1;
  Re_encode_bit( renc, &bm[model], bit ); model = ( model << 1 ) | bit;
  Re_encode_bit( renc, &bm[model], symbol & 1 );
  }

static inline void Re_encode_tree6( struct Range_encoder * const renc,
                                    Bit_model bm[], const unsigned symbol )
  {
  int model = 1;
  bool bit = ( symbol >> 5 ) & 1;
  Re_encode_bit( renc, &bm[model], bit ); model = ( model << 1 ) | bit;
  bit = ( symbol >> 4 ) & 1;
  Re_encode_bit( renc, &bm[model], bit ); model = ( model << 1 ) | bit;
  bit = ( symbol >> 3 ) & 1;
  Re_encode_bit( renc, &bm[model], bit ); model = ( model << 1 ) | bit;
  bit = ( symbol >> 2 ) & 1;
  Re_encode_bit( renc, &bm[model], bit ); model = ( model << 1 ) | bit;
  bit = ( symbol >> 1 ) & 1;
  Re_encode_bit( renc, &bm[model], bit ); model = ( model << 1 ) | bit;
  Re_encode_bit( renc, &bm[model], symbol & 1 );
  }

static inline void Re_encode_tree8( struct Range_encoder * const renc,
                                    Bit_model bm[], const int symbol )
  {
  int model = 1;
  int i;
  for( i = 7; i >= 0; --i )
    {
    const bool bit = ( symbol >> i ) & 1;
    Re_encode_bit( renc, &bm[model], bit );
    model = ( model << 1 ) | bit;
    }
  }

static inline void Re_encode_tree_reversed( struct Range_encoder * const renc,
                     Bit_model bm[], int symbol, const int num_bits )
  {
  int model = 1;
  int i;
  for( i = num_bits; i > 0; --i )
    {
    const bool bit = symbol & 1;
    symbol >>= 1;
    Re_encode_bit( renc, &bm[model], bit );
    model = ( model << 1 ) | bit;
    }
  }

static inline void Re_encode_matched( struct Range_encoder * const renc,
                                      Bit_model bm[], unsigned symbol,
                                      unsigned match_byte )
  {
  unsigned mask = 0x100;
  symbol |= mask;
  while( true )
    {
    const unsigned match_bit = ( match_byte <<= 1 ) & mask;
    const bool bit = ( symbol <<= 1 ) & 0x100;
    Re_encode_bit( renc, &bm[(symbol>>9)+match_bit+mask], bit );
    if( symbol >= 0x10000 ) break;
    mask &= ~(match_bit ^ symbol);	/* if( match_bit != bit ) mask = 0; */
    }
  }

static inline void Re_encode_len( struct Range_encoder * const renc,
                                  struct Len_model * const lm,
                                  int symbol, const int pos_state )
  {
  bool bit = ( ( symbol -= min_match_len ) >= len_low_symbols );
  Re_encode_bit( renc, &lm->choice1, bit );
  if( !bit )
    Re_encode_tree3( renc, lm->bm_low[pos_state], symbol );
  else
    {
    bit = ( ( symbol -= len_low_symbols ) >= len_mid_symbols );
    Re_encode_bit( renc, &lm->choice2, bit );
    if( !bit )
      Re_encode_tree3( renc, lm->bm_mid[pos_state], symbol );
    else
      Re_encode_tree8( renc, lm->bm_high, symbol - len_mid_symbols );
    }
  }


enum { max_marker_size = 16,
       num_rep_distances = 4 };		/* must be 4 */

struct LZ_encoder_base
  {
  struct Matchfinder_base mb;
  unsigned long long member_size_limit;
  uint32_t crc;

  Bit_model bm_literal[1<<literal_context_bits][0x300];
  Bit_model bm_match[states][pos_states];
  Bit_model bm_rep[states];
  Bit_model bm_rep0[states];
  Bit_model bm_rep1[states];
  Bit_model bm_rep2[states];
  Bit_model bm_len[states][pos_states];
  Bit_model bm_dis_slot[len_states][1<<dis_slot_bits];
  Bit_model bm_dis[modeled_distances-end_dis_model+1];
  Bit_model bm_align[dis_align_size];
  struct Len_model match_len_model;
  struct Len_model rep_len_model;
  struct Range_encoder renc;
  int reps[num_rep_distances];
  State state;
  bool member_finished;
  };

static void LZeb_reset( struct LZ_encoder_base * const eb,
                        const unsigned long long member_size );

static inline bool LZeb_init( struct LZ_encoder_base * const eb,
                              const int before, const int dict_size,
                              const int after_size, const int dict_factor,
                              const int num_prev_positions23,
                              const int pos_array_factor,
                              const unsigned min_free_bytes,
                              const unsigned long long member_size )
  {
  if( !Mb_init( &eb->mb, before, dict_size, after_size, dict_factor,
                num_prev_positions23, pos_array_factor ) ) return false;
  if( !Re_init( &eb->renc, eb->mb.dictionary_size, min_free_bytes ) )
    return false;
  LZeb_reset( eb, member_size );
  return true;
  }

static inline bool LZeb_member_finished( const struct LZ_encoder_base * const eb )
  { return ( eb->member_finished && !Cb_used_bytes( &eb->renc.cb ) ); }

static inline void LZeb_free( struct LZ_encoder_base * const eb )
  { Re_free( &eb->renc ); Mb_free( &eb->mb ); }

static inline unsigned LZeb_crc( const struct LZ_encoder_base * const eb )
  { return eb->crc ^ 0xFFFFFFFFU; }

static inline int LZeb_price_literal( const struct LZ_encoder_base * const eb,
                            const uint8_t prev_byte, const uint8_t symbol )
  { return price_symbol8( eb->bm_literal[get_lit_state(prev_byte)], symbol ); }

static inline int LZeb_price_matched( const struct LZ_encoder_base * const eb,
  const uint8_t prev_byte, const uint8_t symbol, const uint8_t match_byte )
  { return price_matched( eb->bm_literal[get_lit_state(prev_byte)], symbol,
                          match_byte ); }

static inline void LZeb_encode_literal( struct LZ_encoder_base * const eb,
                            const uint8_t prev_byte, const uint8_t symbol )
  { Re_encode_tree8( &eb->renc, eb->bm_literal[get_lit_state(prev_byte)],
                     symbol ); }

static inline void LZeb_encode_matched( struct LZ_encoder_base * const eb,
  const uint8_t prev_byte, const uint8_t symbol, const uint8_t match_byte )
  { Re_encode_matched( &eb->renc, eb->bm_literal[get_lit_state(prev_byte)],
                       symbol, match_byte ); }

static inline void LZeb_encode_pair( struct LZ_encoder_base * const eb,
                                     const unsigned dis, const int len,
                                     const int pos_state )
  {
  const unsigned dis_slot = get_slot( dis );
  Re_encode_len( &eb->renc, &eb->match_len_model, len, pos_state );
  Re_encode_tree6( &eb->renc, eb->bm_dis_slot[get_len_state(len)], dis_slot );

  if( dis_slot >= start_dis_model )
    {
    const int direct_bits = ( dis_slot >> 1 ) - 1;
    const unsigned base = ( 2 | ( dis_slot & 1 ) ) << direct_bits;
    const unsigned direct_dis = dis - base;

    if( dis_slot < end_dis_model )
      Re_encode_tree_reversed( &eb->renc, eb->bm_dis + ( base - dis_slot ),
                               direct_dis, direct_bits );
    else
      {
      Re_encode( &eb->renc, direct_dis >> dis_align_bits,
                 direct_bits - dis_align_bits );
      Re_encode_tree_reversed( &eb->renc, eb->bm_align, direct_dis, dis_align_bits );
      }
    }
  }
