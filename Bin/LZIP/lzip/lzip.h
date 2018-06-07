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

class State
  {
  int st;

public:
  enum { states = 12 };
  State() : st( 0 ) {}
  int operator()() const { return st; }
  bool is_char() const { return st < 7; }

  void set_char()
    {
    static const int next[states] = { 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 4, 5 };
    st = next[st];
    }
  bool is_char_set_char()
    {
    if( st < 7 ) { st -= ( st < 4 ) ? st : 3; return true; }
    else { st -= ( st < 10 ) ? 3 : 6; return false; }
    }
  void set_char_rep()  { st = 8; }
  void set_match()     { st = ( st < 7 ) ? 7 : 10; }
  void set_rep()       { st = ( st < 7 ) ? 8 : 11; }
  void set_short_rep() { st = ( st < 7 ) ? 9 : 11; }
  };


enum {
  min_dictionary_bits = 12,
  min_dictionary_size = 1 << min_dictionary_bits,	// >= modeled_distances
  max_dictionary_bits = 29,
  max_dictionary_size = 1 << max_dictionary_bits,
  min_member_size = 36,
  literal_context_bits = 3,
  literal_pos_state_bits = 0,				// not used
  pos_state_bits = 2,
  pos_states = 1 << pos_state_bits,
  pos_state_mask = pos_states - 1,

  len_states = 4,
  dis_slot_bits = 6,
  start_dis_model = 4,
  end_dis_model = 14,
  modeled_distances = 1 << (end_dis_model / 2),		// 128
  dis_align_bits = 4,
  dis_align_size = 1 << dis_align_bits,

  len_low_bits = 3,
  len_mid_bits = 3,
  len_high_bits = 8,
  len_low_symbols = 1 << len_low_bits,
  len_mid_symbols = 1 << len_mid_bits,
  len_high_symbols = 1 << len_high_bits,
  max_len_symbols = len_low_symbols + len_mid_symbols + len_high_symbols,

  min_match_len = 2,					// must be 2
  max_match_len = min_match_len + max_len_symbols - 1,	// 273
  min_match_len_limit = 5 };

inline int get_len_state( const int len )
  { return std::min( len - min_match_len, len_states - 1 ); }

inline int get_lit_state( const uint8_t prev_byte )
  { return ( prev_byte >> ( 8 - literal_context_bits ) ); }


enum { bit_model_move_bits = 5,
       bit_model_total_bits = 11,
       bit_model_total = 1 << bit_model_total_bits };

struct Bit_model
  {
  int probability;
  void reset() { probability = bit_model_total / 2; }
  void reset( const int size )
    { for( int i = 0; i < size; ++i ) this[i].reset(); }
  Bit_model() { reset(); }
  };

struct Len_model
  {
  Bit_model choice1;
  Bit_model choice2;
  Bit_model bm_low[pos_states][len_low_symbols];
  Bit_model bm_mid[pos_states][len_mid_symbols];
  Bit_model bm_high[len_high_symbols];

  void reset()
    {
    choice1.reset();
    choice2.reset();
    bm_low[0][0].reset( pos_states * len_low_symbols );
    bm_mid[0][0].reset( pos_states * len_mid_symbols );
    bm_high[0].reset( len_high_symbols );
    }
  };


class Pretty_print
  {
  std::string name_;
  const char * const stdin_name;
  unsigned longest_name;
  mutable bool first_post;

public:
  Pretty_print( const std::vector< std::string > & filenames,
                const int verbosity )
    : stdin_name( "(stdin)" ), longest_name( 0 ), first_post( false )
    {
    if( verbosity <= 0 ) return;
    const unsigned stdin_name_len = std::strlen( stdin_name );
    for( unsigned i = 0; i < filenames.size(); ++i )
      {
      const std::string & s = filenames[i];
      const unsigned len = ( s == "-" ) ? stdin_name_len : s.size();
      if( len > longest_name ) longest_name = len;
      }
    if( longest_name == 0 ) longest_name = stdin_name_len;
    }

  void set_name( const std::string & filename )
    {
    if( filename.size() && filename != "-" ) name_ = filename;
    else name_ = stdin_name;
    first_post = true;
    }

  void reset() const { if( name_.size() ) first_post = true; }
  const char * name() const { return name_.c_str(); }
  void operator()( const char * const msg = 0 ) const;
  };


class CRC32
  {
  uint32_t data[256];		// Table of CRCs of all 8-bit messages.

public:
  CRC32()
    {
    for( unsigned n = 0; n < 256; ++n )
      {
      unsigned c = n;
      for( int k = 0; k < 8; ++k )
        { if( c & 1 ) c = 0xEDB88320U ^ ( c >> 1 ); else c >>= 1; }
      data[n] = c;
      }
    }

  uint32_t operator[]( const uint8_t byte ) const { return data[byte]; }

  void update_byte( uint32_t & crc, const uint8_t byte ) const
    { crc = data[(crc^byte)&0xFF] ^ ( crc >> 8 ); }

  void update_buf( uint32_t & crc, const uint8_t * const buffer,
                   const int size ) const
    {
    uint32_t c = crc;
    for( int i = 0; i < size; ++i )
      c = data[(c^buffer[i])&0xFF] ^ ( c >> 8 );
    crc = c;
    }
  };

extern const CRC32 crc32;


inline bool isvalid_ds( const unsigned dictionary_size )
  { return ( dictionary_size >= min_dictionary_size &&
             dictionary_size <= max_dictionary_size ); }


inline int real_bits( unsigned value )
  {
  int bits = 0;
  while( value > 0 ) { value >>= 1; ++bits; }
  return bits;
  }


const uint8_t magic_string[4] = { 0x4C, 0x5A, 0x49, 0x50 };	// "LZIP"

struct File_header
  {
  uint8_t data[6];			// 0-3 magic bytes
					//   4 version
					//   5 coded_dict_size
  enum { size = 6 };

  void set_magic() { std::memcpy( data, magic_string, 4 ); data[4] = 1; }
  bool verify_magic() const
    { return ( std::memcmp( data, magic_string, 4 ) == 0 ); }
  bool verify_prefix( const int size ) const	// detect truncated header
    {
    for( int i = 0; i < size && i < 4; ++i )
      if( data[i] != magic_string[i] ) return false;
    return ( size > 0 );
    }

  uint8_t version() const { return data[4]; }
  bool verify_version() const { return ( data[4] == 1 ); }

  unsigned dictionary_size() const
    {
    unsigned sz = ( 1 << ( data[5] & 0x1F ) );
    if( sz > min_dictionary_size )
      sz -= ( sz / 16 ) * ( ( data[5] >> 5 ) & 7 );
    return sz;
    }

  bool dictionary_size( const unsigned sz )
    {
    if( !isvalid_ds( sz ) ) return false;
    data[5] = real_bits( sz - 1 );
    if( sz > min_dictionary_size )
      {
      const unsigned base_size = 1 << data[5];
      const unsigned fraction = base_size / 16;
      for( unsigned i = 7; i >= 1; --i )
        if( base_size - ( i * fraction ) >= sz )
          { data[5] |= ( i << 5 ); break; }
      }
    return true;
    }
  };


struct File_trailer
  {
  uint8_t data[20];	//  0-3  CRC32 of the uncompressed data
			//  4-11 size of the uncompressed data
			// 12-19 member size including header and trailer

  enum { size = 20 };

  unsigned data_crc() const
    {
    unsigned tmp = 0;
    for( int i = 3; i >= 0; --i ) { tmp <<= 8; tmp += data[i]; }
    return tmp;
    }

  void data_crc( unsigned crc )
    { for( int i = 0; i <= 3; ++i ) { data[i] = (uint8_t)crc; crc >>= 8; } }

  unsigned long long data_size() const
    {
    unsigned long long tmp = 0;
    for( int i = 11; i >= 4; --i ) { tmp <<= 8; tmp += data[i]; }
    return tmp;
    }

  void data_size( unsigned long long sz )
    { for( int i = 4; i <= 11; ++i ) { data[i] = (uint8_t)sz; sz >>= 8; } }

  unsigned long long member_size() const
    {
    unsigned long long tmp = 0;
    for( int i = 19; i >= 12; --i ) { tmp <<= 8; tmp += data[i]; }
    return tmp;
    }

  void member_size( unsigned long long sz )
    { for( int i = 12; i <= 19; ++i ) { data[i] = (uint8_t)sz; sz >>= 8; } }
  };


struct Error
  {
  const char * const msg;
  explicit Error( const char * const s ) : msg( s ) {}
  };


const char * const bad_magic_msg = "Bad magic number (file not in lzip format).";
const char * const bad_dict_msg = "Invalid dictionary size in member header.";
const char * const trailing_msg = "Trailing data not allowed.";

// defined in decoder.cc
int readblock( const int fd, uint8_t * const buf, const int size );
int writeblock( const int fd, const uint8_t * const buf, const int size );

// defined in list.cc
int list_files( const std::vector< std::string > & filenames,
                const bool ignore_trailing );

// defined in main.cc
extern int verbosity;
struct stat;
const char * bad_version( const unsigned version );
const char * format_ds( const unsigned dictionary_size );
int open_instream( const char * const name, struct stat * const in_statsp,
                   const bool no_ofile, const bool reg_only = false );
void show_error( const char * const msg, const int errcode = 0,
                 const bool help = false );
void show_file_error( const char * const filename, const char * const msg,
                      const int errcode = 0 );
void internal_error( const char * const msg );
class Matchfinder_base;
void show_progress( const unsigned long long partial_size = 0,
                    const Matchfinder_base * const m = 0,
                    const Pretty_print * const p = 0,
                    const unsigned long long cfile_size = 0 );
