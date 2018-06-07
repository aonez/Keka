/*  Plzip - Parallel compressor compatible with lzip
    Copyright (C) 2009-2017 Antonio Diaz Diaz.

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

#include <sys/types.h>

#ifndef LZ_API_VERSION
#define LZ_API_VERSION 1
#endif

enum {
  min_dictionary_bits = 12,
  min_dictionary_size = 1 << min_dictionary_bits,
  max_dictionary_bits = 29,
  max_dictionary_size = 1 << max_dictionary_bits,
  min_member_size = 36 };


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


const char * const bad_magic_msg = "Bad magic number (file not in lzip format).";
const char * const bad_dict_msg = "Invalid dictionary size in member header.";
const char * const trailing_msg = "Trailing data not allowed.";

// defined in compress.cc
int readblock( const int fd, uint8_t * const buf, const int size );
int writeblock( const int fd, const uint8_t * const buf, const int size );
void xinit_mutex( pthread_mutex_t * const mutex );
void xinit_cond( pthread_cond_t * const cond );
void xdestroy_mutex( pthread_mutex_t * const mutex );
void xdestroy_cond( pthread_cond_t * const cond );
void xlock( pthread_mutex_t * const mutex );
void xunlock( pthread_mutex_t * const mutex );
void xwait( pthread_cond_t * const cond, pthread_mutex_t * const mutex );
void xsignal( pthread_cond_t * const cond );
void xbroadcast( pthread_cond_t * const cond );
int compress( const int data_size, const int dictionary_size,
              const int match_len_limit, const int num_workers,
              const int infd, const int outfd,
              const Pretty_print & pp, const int debug_level );

// defined in file_index.cc
class File_index;

// defined in dec_stdout.cc
int dec_stdout( const int num_workers, const int infd, const int outfd,
                const Pretty_print & pp, const int debug_level,
                const File_index & file_index );

// defined in dec_stream.cc
int dec_stream( const int num_workers, const int infd, const int outfd,
                const Pretty_print & pp, const int debug_level,
                const bool ignore_trailing );

// defined in decompress.cc
int preadblock( const int fd, uint8_t * const buf, const int size,
                const long long pos );
int decompress_read_error( struct LZ_Decoder * const decoder,
                           const Pretty_print & pp, const int worker_id );
int decompress( int num_workers, const int infd, const int outfd,
                const Pretty_print & pp, const int debug_level,
                const bool ignore_trailing, const bool infd_isreg );

// defined in list.cc
int list_files( const std::vector< std::string > & filenames,
                const bool ignore_trailing );

// defined in main.cc
extern int verbosity;
struct stat;
const char * bad_version( const unsigned version );
const char * format_ds( const unsigned dictionary_size );
void show_header( const unsigned dictionary_size );
int open_instream( const char * const name, struct stat * const in_statsp,
                   const bool no_ofile, const bool reg_only = false );
void cleanup_and_fail( const int retval = 1 );	// terminate the program
void show_error( const char * const msg, const int errcode = 0,
                 const bool help = false );
void show_file_error( const char * const filename, const char * const msg,
                      const int errcode = 0 );
void internal_error( const char * const msg );
void show_progress( const int packet_size,
                    const Pretty_print * const p = 0,
                    const unsigned long long cfile_size = 0 );


class Slot_tally
  {
  const int num_slots;				// total slots
  int num_free;					// remaining free slots
  pthread_mutex_t mutex;
  pthread_cond_t slot_av;			// slot available

  Slot_tally( const Slot_tally & );		// declared as private
  void operator=( const Slot_tally & );		// declared as private

public:
  explicit Slot_tally( const int slots )
    : num_slots( slots ), num_free( slots )
    { xinit_mutex( &mutex ); xinit_cond( &slot_av ); }

  ~Slot_tally() { xdestroy_cond( &slot_av ); xdestroy_mutex( &mutex ); }

  bool all_free() { return ( num_free == num_slots ); }

  void get_slot()				// wait for a free slot
    {
    xlock( &mutex );
    while( num_free <= 0 ) xwait( &slot_av, &mutex );
    --num_free;
    xunlock( &mutex );
    }

  void leave_slot()				// return a slot to the tally
    {
    xlock( &mutex );
    if( ++num_free == 1 ) xsignal( &slot_av );	// num_free was 0
    xunlock( &mutex );
    }

  void leave_slots( const int slots )		// return slots to the tally
    {
    xlock( &mutex );
    num_free += slots;
    if( num_free == slots ) xsignal( &slot_av );	// num_free was 0
    xunlock( &mutex );
    }
  };
