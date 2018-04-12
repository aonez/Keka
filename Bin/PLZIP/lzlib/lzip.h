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

#ifndef max
  #define max(x,y) ((x) >= (y) ? (x) : (y))
#endif
#ifndef min
  #define min(x,y) ((x) <= (y) ? (x) : (y))
#endif

typedef int State;

enum { states = 12 };

static inline bool St_is_char( const State st ) { return st < 7; }

static inline State St_set_char( const State st )
  {
  static const State next[states] = { 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 4, 5 };
  return next[st];
  }

static inline State St_set_match( const State st )
  { return ( ( st < 7 ) ? 7 : 10 ); }

static inline State St_set_rep( const State st )
  { return ( ( st < 7 ) ? 8 : 11 ); }

static inline State St_set_short_rep( const State st )
  { return ( ( st < 7 ) ? 9 : 11 ); }


enum {
  min_dictionary_bits = 12,
  min_dictionary_size = 1 << min_dictionary_bits,	/* >= modeled_distances */
  max_dictionary_bits = 29,
  max_dictionary_size = 1 << max_dictionary_bits,
  literal_context_bits = 3,
  literal_pos_state_bits = 0,				/* not used */
  pos_state_bits = 2,
  pos_states = 1 << pos_state_bits,
  pos_state_mask = pos_states - 1,

  len_states = 4,
  dis_slot_bits = 6,
  start_dis_model = 4,
  end_dis_model = 14,
  modeled_distances = 1 << (end_dis_model / 2),		/* 128 */
  dis_align_bits = 4,
  dis_align_size = 1 << dis_align_bits,

  len_low_bits = 3,
  len_mid_bits = 3,
  len_high_bits = 8,
  len_low_symbols = 1 << len_low_bits,
  len_mid_symbols = 1 << len_mid_bits,
  len_high_symbols = 1 << len_high_bits,
  max_len_symbols = len_low_symbols + len_mid_symbols + len_high_symbols,

  min_match_len = 2,					/* must be 2 */
  max_match_len = min_match_len + max_len_symbols - 1,	/* 273 */
  min_match_len_limit = 5 };

static inline int get_len_state( const int len )
  { return min( len - min_match_len, len_states - 1 ); }

static inline int get_lit_state( const uint8_t prev_byte )
  { return ( prev_byte >> ( 8 - literal_context_bits ) ); }


enum { bit_model_move_bits = 5,
       bit_model_total_bits = 11,
       bit_model_total = 1 << bit_model_total_bits };

typedef int Bit_model;

static inline void Bm_init( Bit_model * const probability )
  { *probability = bit_model_total / 2; }

static inline void Bm_array_init( Bit_model bm[], const int size )
  { int i; for( i = 0; i < size; ++i ) Bm_init( &bm[i] ); }

struct Len_model
  {
  Bit_model choice1;
  Bit_model choice2;
  Bit_model bm_low[pos_states][len_low_symbols];
  Bit_model bm_mid[pos_states][len_mid_symbols];
  Bit_model bm_high[len_high_symbols];
  };

static inline void Lm_init( struct Len_model * const lm )
  {
  Bm_init( &lm->choice1 );
  Bm_init( &lm->choice2 );
  Bm_array_init( lm->bm_low[0], pos_states * len_low_symbols );
  Bm_array_init( lm->bm_mid[0], pos_states * len_mid_symbols );
  Bm_array_init( lm->bm_high, len_high_symbols );
  }


/* Table of CRCs of all 8-bit messages. */
static const uint32_t crc32[256] =
  {
  0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
  0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
  0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
  0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
  0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
  0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
  0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
  0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
  0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
  0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
  0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
  0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
  0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
  0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
  0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
  0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
  0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
  0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
  0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
  0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
  0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
  0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
  0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
  0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
  0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
  0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
  0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
  0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
  0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
  0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
  0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
  0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
  0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
  0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
  0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
  0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
  0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
  0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
  0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
  0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
  0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693,
  0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
  0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D };


static inline void CRC32_update_byte( uint32_t * const crc, const uint8_t byte )
  { *crc = crc32[(*crc^byte)&0xFF] ^ ( *crc >> 8 ); }

static inline void CRC32_update_buf( uint32_t * const crc,
                                     const uint8_t * const buffer,
                                     const int size )
  {
  int i;
  uint32_t c = *crc;
  for( i = 0; i < size; ++i )
    c = crc32[(c^buffer[i])&0xFF] ^ ( c >> 8 );
  *crc = c;
  }


static inline bool isvalid_ds( const unsigned dictionary_size )
  { return ( dictionary_size >= min_dictionary_size &&
             dictionary_size <= max_dictionary_size ); }


static inline int real_bits( unsigned value )
  {
  int bits = 0;
  while( value > 0 ) { value >>= 1; ++bits; }
  return bits;
  }


static const uint8_t magic_string[4] = { 0x4C, 0x5A, 0x49, 0x50 }; /* "LZIP" */

typedef uint8_t File_header[6];		/* 0-3 magic bytes */
					/*   4 version */
					/*   5 coded_dict_size */
enum { Fh_size = 6 };

static inline void Fh_set_magic( File_header data )
  { memcpy( data, magic_string, 4 ); data[4] = 1; }

static inline bool Fh_verify_magic( const File_header data )
  { return ( memcmp( data, magic_string, 4 ) == 0 ); }

/* detect truncated header */
static inline bool Fh_verify_prefix( const File_header data, const int size )
  {
  int i; for( i = 0; i < size && i < 4; ++i )
    if( data[i] != magic_string[i] ) return false;
  return ( size > 0 );
  }

static inline uint8_t Fh_version( const File_header data )
  { return data[4]; }

static inline bool Fh_verify_version( const File_header data )
  { return ( data[4] == 1 ); }

static inline unsigned Fh_get_dictionary_size( const File_header data )
  {
  unsigned sz = ( 1 << ( data[5] & 0x1F ) );
  if( sz > min_dictionary_size )
    sz -= ( sz / 16 ) * ( ( data[5] >> 5 ) & 7 );
  return sz;
  }

static inline bool Fh_set_dictionary_size( File_header data, const unsigned sz )
  {
  if( !isvalid_ds( sz ) ) return false;
  data[5] = real_bits( sz - 1 );
  if( sz > min_dictionary_size )
    {
    const unsigned base_size = 1 << data[5];
    const unsigned fraction = base_size / 16;
    unsigned i;
    for( i = 7; i >= 1; --i )
      if( base_size - ( i * fraction ) >= sz )
        { data[5] |= ( i << 5 ); break; }
    }
  return true;
  }

static inline bool Fh_verify( const File_header data )
  {
  if( Fh_verify_magic( data ) && Fh_verify_version( data ) )
    return isvalid_ds( Fh_get_dictionary_size( data ) );
  return false;
  }


typedef uint8_t File_trailer[20];
			/*  0-3  CRC32 of the uncompressed data */
			/*  4-11 size of the uncompressed data */
			/* 12-19 member size including header and trailer */

enum { Ft_size = 20 };

static inline unsigned Ft_get_data_crc( const File_trailer data )
  {
  unsigned tmp = 0;
  int i; for( i = 3; i >= 0; --i ) { tmp <<= 8; tmp += data[i]; }
  return tmp;
  }

static inline void Ft_set_data_crc( File_trailer data, unsigned crc )
  { int i; for( i = 0; i <= 3; ++i ) { data[i] = (uint8_t)crc; crc >>= 8; } }

static inline unsigned long long Ft_get_data_size( const File_trailer data )
  {
  unsigned long long tmp = 0;
  int i; for( i = 11; i >= 4; --i ) { tmp <<= 8; tmp += data[i]; }
  return tmp;
  }

static inline void Ft_set_data_size( File_trailer data, unsigned long long sz )
  { int i; for( i = 4; i <= 11; ++i ) { data[i] = (uint8_t)sz; sz >>= 8; } }

static inline unsigned long long Ft_get_member_size( const File_trailer data )
  {
  unsigned long long tmp = 0;
  int i; for( i = 19; i >= 12; --i ) { tmp <<= 8; tmp += data[i]; }
  return tmp;
  }

static inline void Ft_set_member_size( File_trailer data, unsigned long long sz )
  { int i; for( i = 12; i <= 19; ++i ) { data[i] = (uint8_t)sz; sz >>= 8; } }
