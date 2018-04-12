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
/*
    Exit status: 0 for a normal exit, 1 for environmental problems
    (file not found, invalid flags, I/O errors, etc), 2 to indicate a
    corrupt or invalid input file, 3 for an internal consistency error
    (eg, bug) which caused lzip to panic.
*/

#define _FILE_OFFSET_BITS 64

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <utime.h>
#include <sys/stat.h>
#if defined(__MSVCRT__)
#include <io.h>
#define fchmod(x,y) 0
#define fchown(x,y,z) 0
#define strtoull std::strtoul
#define SIGHUP SIGTERM
#define S_ISSOCK(x) 0
#define S_IRGRP 0
#define S_IWGRP 0
#define S_IROTH 0
#define S_IWOTH 0
#endif
#if defined(__OS2__)
#include <io.h>
#endif

#include "arg_parser.h"
#include "lzip.h"
#include "decoder.h"
#include "encoder_base.h"
#include "encoder.h"
#include "fast_encoder.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

#if CHAR_BIT != 8
#error "Environments where CHAR_BIT != 8 are not supported."
#endif

int verbosity = 0;
bool kverbosity = false;

namespace {

const char * const Program_name = "Lzip";
const char * const program_name = "lzip";
const char * const program_year = "2017";
const char * invocation_name = 0;

const struct { const char * from; const char * to; } known_extensions[] = {
  { ".lz",  ""     },
  { ".tlz", ".tar" },
  { 0,      0      } };

struct Lzma_options
  {
  int dictionary_size;		// 4 KiB .. 512 MiB
  int match_len_limit;		// 5 .. 273
  };

enum Mode { m_compress, m_decompress, m_list, m_test };

std::string output_filename;
std::string output_write;
int outfd = -1;
bool delete_output_on_interrupt = false;


void show_help()
  {
  std::printf( "%s - LZMA lossless data compressor.\n", Program_name );
  std::printf( "\nUsage: %s [options] [files]\n", invocation_name );
  std::printf( "\nOptions:\n"
               "  -h, --help                     display this help and exit\n"
               "  -V, --version                  output version information and exit\n"
               "  -a, --trailing-error           exit with error status if trailing data\n"
               "  -b, --member-size=<bytes>      set member size limit in bytes\n"
               "  -c, --stdout                   write to standard output, keep input files\n"
               "  -d, --decompress               decompress\n"
               "  -f, --force                    overwrite existing output files\n"
               "  -F, --recompress               force re-compression of compressed files\n"
               "  -k, --keep                     keep (don't delete) input files\n"
               "  -l, --list                     print (un)compressed file sizes\n"
               "  -m, --match-length=<bytes>     set match length limit in bytes [36]\n"
               "  -o, --output=<file>            if reading standard input, write to <file>\n"
               "  -O, --output-write=<file|path> compress to <file>, decompress to <path>\n"
               "  -q, --quiet                    suppress all messages\n"
               "  -s, --dictionary-size=<bytes>  set dictionary size limit in bytes [8 MiB]\n"
               "  -S, --volume-size=<bytes>      set volume size limit in bytes\n"
               "  -t, --test                     test compressed file integrity\n"
               "  -v, --verbose                  be verbose (a 2nd -v gives more)\n"
               "  -K, --keka-verbose             print progress direclty\n"
               "  -0 .. -9                       set compression level [default 6]\n"
               "      --fast                     alias for -0\n"
               "      --best                     alias for -9\n"
               "If no file names are given, or if a file is '-', lzip compresses or\n"
               "decompresses from standard input to standard output.\n"
               "Numbers may be followed by a multiplier: k = kB = 10^3 = 1000,\n"
               "Ki = KiB = 2^10 = 1024, M = 10^6, Mi = 2^20, G = 10^9, Gi = 2^30, etc...\n"
               "Dictionary sizes 12 to 29 are interpreted as powers of two, meaning 2^12\n"
               "to 2^29 bytes.\n"
               "\nThe bidimensional parameter space of LZMA can't be mapped to a linear\n"
               "scale optimal for all files. If your files are large, very repetitive,\n"
               "etc, you may need to use the --dictionary-size and --match-length\n"
               "options directly to achieve optimal performance.\n"
               "\nExit status: 0 for a normal exit, 1 for environmental problems (file\n"
               "not found, invalid flags, I/O errors, etc), 2 to indicate a corrupt or\n"
               "invalid input file, 3 for an internal consistency error (eg, bug) which\n"
               "caused lzip to panic.\n"
               "\nReport bugs to lzip-bug@nongnu.org\n"
               "Lzip home page: http://www.nongnu.org/lzip/lzip.html\n" );
  }


void show_version()
  {
  std::printf( "%s %s\n", program_name, PROGVERSION );
  std::printf( "Copyright (C) %s Antonio Diaz Diaz.\n", program_year );
  std::printf( "Modified by aONe for use in Keka.\n" );
  std::printf( "License GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl.html>\n"
               "This is free software: you are free to change and redistribute it.\n"
               "There is NO WARRANTY, to the extent permitted by law.\n" );
  }

} // end namespace

const char * bad_version( const unsigned version )
  {
  static char buf[80];
  snprintf( buf, sizeof buf, "Version %u member format not supported.",
            version );
  return buf;
  }


const char * format_ds( const unsigned dictionary_size )
  {
  enum { bufsize = 16, factor = 1024 };
  static char buf[bufsize];
  const char * const prefix[8] =
    { "Ki", "Mi", "Gi", "Ti", "Pi", "Ei", "Zi", "Yi" };
  const char * p = "";
  const char * np = "  ";
  unsigned num = dictionary_size;
  bool exact = ( num % factor == 0 );

  for( int i = 0; i < 8 && ( num > 9999 || ( exact && num >= factor ) ); ++i )
    { num /= factor; if( num % factor != 0 ) exact = false;
      p = prefix[i]; np = ""; }
  snprintf( buf, bufsize, "%s%4u %sB", np, num, p );
  return buf;
  }

namespace {

void show_header( const unsigned dictionary_size )
  {
  if( verbosity >= 3 )
    std::fprintf( stderr, "dictionary %s.  ", format_ds( dictionary_size ) );
  }


unsigned long long getnum( const char * const ptr,
                           const unsigned long long llimit,
                           const unsigned long long ulimit )
  {
  char * tail;
  errno = 0;
  unsigned long long result = strtoull( ptr, &tail, 0 );
  if( tail == ptr )
    {
    show_error( "Bad or missing numerical argument.", 0, true );
    std::exit( 1 );
    }

  if( !errno && tail[0] )
    {
    const unsigned factor = ( tail[1] == 'i' ) ? 1024 : 1000;
    int exponent = 0;				// 0 = bad multiplier
    switch( tail[0] )
      {
      case 'Y': exponent = 8; break;
      case 'Z': exponent = 7; break;
      case 'E': exponent = 6; break;
      case 'P': exponent = 5; break;
      case 'T': exponent = 4; break;
      case 'G': exponent = 3; break;
      case 'M': exponent = 2; break;
      case 'K': if( factor == 1024 ) exponent = 1; break;
      case 'k': if( factor == 1000 ) exponent = 1; break;
      }
    if( exponent <= 0 )
      {
      show_error( "Bad multiplier in numerical argument.", 0, true );
      std::exit( 1 );
      }
    for( int i = 0; i < exponent; ++i )
      {
      if( ulimit / factor >= result ) result *= factor;
      else { errno = ERANGE; break; }
      }
    }
  if( !errno && ( result < llimit || result > ulimit ) ) errno = ERANGE;
  if( errno )
    {
    show_error( "Numerical argument out of limits." );
    std::exit( 1 );
    }
  return result;
  }


int get_dict_size( const char * const arg )
  {
  char * tail;
  const long bits = std::strtol( arg, &tail, 0 );
  if( bits >= min_dictionary_bits &&
      bits <= max_dictionary_bits && *tail == 0 )
    return ( 1 << bits );
  return getnum( arg, min_dictionary_size, max_dictionary_size );
  }


void set_mode( Mode & program_mode, const Mode new_mode )
  {
  if( program_mode != m_compress && program_mode != new_mode )
    {
    show_error( "Only one operation can be specified.", 0, true );
    std::exit( 1 );
    }
  program_mode = new_mode;
  }


int extension_index( const std::string & name )
  {
  for( int eindex = 0; known_extensions[eindex].from; ++eindex )
    {
    const std::string ext( known_extensions[eindex].from );
    if( name.size() > ext.size() &&
        name.compare( name.size() - ext.size(), ext.size(), ext ) == 0 )
      return eindex;
    }
  return -1;
  }

} // end namespace

int open_instream( const char * const name, struct stat * const in_statsp,
                   const bool no_ofile, const bool reg_only )
  {
  int infd = open( name, O_RDONLY | O_BINARY );
  if( infd < 0 )
    show_file_error( name, "Can't open input file", errno );
  else
    {
    const int i = fstat( infd, in_statsp );
    const mode_t mode = in_statsp->st_mode;
    const bool can_read = ( i == 0 && !reg_only &&
                            ( S_ISBLK( mode ) || S_ISCHR( mode ) ||
                              S_ISFIFO( mode ) || S_ISSOCK( mode ) ) );
    if( i != 0 || ( !S_ISREG( mode ) && ( !can_read || !no_ofile ) ) )
      {
      if( verbosity >= 0 )
        std::fprintf( stderr, "%s: Input file '%s' is not a regular file%s.\n",
                      program_name, name,
                      ( can_read && !no_ofile ) ?
                      ",\n      and '--stdout' was not specified" : "" );
      close( infd );
      infd = -1;
      }
    }
  return infd;
  }

namespace {

int open_instream2( const char * const name, struct stat * const in_statsp,
                    const Mode program_mode, const int eindex,
                    const bool recompress, const bool to_stdout )
  {
  if( program_mode == m_compress && !recompress && eindex >= 0 )
    {
    if( verbosity >= 0 )
      std::fprintf( stderr, "%s: Input file '%s' already has '%s' suffix.\n",
                    program_name, name, known_extensions[eindex].from );
    return -1;
    }
  const bool no_ofile = ( to_stdout || program_mode == m_test );
  return open_instream( name, in_statsp, no_ofile, false );
  }


void set_c_outname( const std::string & name, const bool multifile )
  {
      if( !output_write.empty() )
        {
        output_filename = output_write;
        if( multifile )
          {
          const int eindex = extension_index( output_filename );
          if( eindex > -1 )
            {
            const std::string from( known_extensions[eindex].from );
            if( output_filename.size() > from.size() )
              {
              output_filename.assign( output_filename, 0, output_filename.size() - from.size() );
              output_filename += "00001";
              output_filename += known_extensions[eindex].from;
              }
            }
          else
            output_filename += "00001";
          }
        }
      else
        {
          output_filename = name;
          if( multifile ) output_filename += "00001";
          output_filename += known_extensions[0].from;
        }
  }

void set_d_outname( const std::string & name, const int eindex )
  {
  if( eindex >= 0 )
    {
    const std::string from( known_extensions[eindex].from );
    if( name.size() > from.size() )
      {
      output_filename.assign( name, 0, name.size() - from.size() );
      output_filename += known_extensions[eindex].to;
      if( !output_write.empty() )
        {
        while (output_write.substr(output_write.size()-1, output_write.size()) == "/")
          output_write = output_write.substr(0, output_write.size()-1);
        output_filename = output_write + output_filename.substr(output_filename.find_last_of("/"), output_filename.size());
        }
      return;
      }
    }
  output_filename = name; output_filename += ".out";
  if( verbosity >= 1 )
    std::fprintf( stderr, "%s: Can't guess original name for '%s' -- using '%s'\n",
                  program_name, name.c_str(), output_filename.c_str() );
  }


bool open_outstream( const bool force, const bool from_stdin )
  {
  const mode_t usr_rw = S_IRUSR | S_IWUSR;
  const mode_t all_rw = usr_rw | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
  const mode_t outfd_mode = from_stdin ? all_rw : usr_rw;
  int flags = O_CREAT | O_WRONLY | O_BINARY;
  if( force ) flags |= O_TRUNC; else flags |= O_EXCL;

  outfd = open( output_filename.c_str(), flags, outfd_mode );
  if( outfd >= 0 ) delete_output_on_interrupt = true;
  else if( verbosity >= 0 )
    {
    if( errno == EEXIST )
      std::fprintf( stderr, "%s: Output file '%s' already exists, skipping.\n",
                    program_name, output_filename.c_str() );
    else
      std::fprintf( stderr, "%s: Can't create output file '%s': %s\n",
                    program_name, output_filename.c_str(), std::strerror( errno ) );
    }
  return ( outfd >= 0 );
  }


bool check_tty( const char * const input_filename, const int infd,
                const Mode program_mode )
  {
  if( program_mode == m_compress && isatty( outfd ) )
    {
    show_error( "I won't write compressed data to a terminal.", 0, true );
    return false;
    }
  if( ( program_mode == m_decompress || program_mode == m_test ) &&
      isatty( infd ) )
    {
    show_file_error( input_filename,
                     "I won't read compressed data from a terminal." );
    return false;
    }
  return true;
  }


void cleanup_and_fail( const int retval )
  {
  if( delete_output_on_interrupt )
    {
    delete_output_on_interrupt = false;
    if( verbosity >= 0 )
      std::fprintf( stderr, "%s: Deleting output file '%s', if it exists.\n",
                    program_name, output_filename.c_str() );
    if( outfd >= 0 ) { close( outfd ); outfd = -1; }
    if( std::remove( output_filename.c_str() ) != 0 && errno != ENOENT )
      show_error( "WARNING: deletion of output file (apparently) failed." );
    }
  std::exit( retval );
  }


     // Set permissions, owner and times.
void close_and_set_permissions( const struct stat * const in_statsp )
  {
  bool warning = false;
  if( in_statsp )
    {
    const mode_t mode = in_statsp->st_mode;
    // fchown will in many cases return with EPERM, which can be safely ignored.
    if( fchown( outfd, in_statsp->st_uid, in_statsp->st_gid ) == 0 )
      { if( fchmod( outfd, mode ) != 0 ) warning = true; }
    else
      if( errno != EPERM ||
          fchmod( outfd, mode & ~( S_ISUID | S_ISGID | S_ISVTX ) ) != 0 )
        warning = true;
    }
  if( close( outfd ) != 0 )
    {
    show_error( "Error closing output file", errno );
    cleanup_and_fail( 1 );
    }
  outfd = -1;
  delete_output_on_interrupt = false;
  if( in_statsp )
    {
    struct utimbuf t;
    t.actime = in_statsp->st_atime;
    t.modtime = in_statsp->st_mtime;
    if( utime( output_filename.c_str(), &t ) != 0 ) warning = true;
    }
  if( warning && verbosity >= 1 )
    show_error( "Can't change output file attributes." );
  }


bool next_filename()
  {
  const unsigned name_len = output_filename.size();
  const unsigned ext_len = std::strlen( known_extensions[0].from );
  if( name_len >= ext_len + 5 )				// "*00001.lz"
    for( int i = name_len - ext_len - 1, j = 0; j < 5; --i, ++j )
      {
      if( output_filename[i] < '9' ) { ++output_filename[i]; return true; }
      else output_filename[i] = '0';
      }
  return false;
  }


int compress( const unsigned long long member_size,
              const unsigned long long volume_size, const int infd,
              const Lzma_options & encoder_options, const Pretty_print & pp,
              const struct stat * const in_statsp, const bool zero )
  {
  const unsigned long long cfile_size =
    (in_statsp && S_ISREG( in_statsp->st_mode )) ? in_statsp->st_size / 100 : 0;
  int retval = 0;
  LZ_encoder_base * encoder = 0;		// polymorphic encoder
  if( verbosity >= 1 ) pp();

  try {
    if( zero )
      encoder = new FLZ_encoder( infd, outfd );
    else
      {
      File_header header;
      if( header.dictionary_size( encoder_options.dictionary_size ) &&
          encoder_options.match_len_limit >= min_match_len_limit &&
          encoder_options.match_len_limit <= max_match_len )
        encoder = new LZ_encoder( header.dictionary_size(),
                                  encoder_options.match_len_limit, infd, outfd );
      else internal_error( "invalid argument to encoder." );
      }

    unsigned long long in_size = 0, out_size = 0, partial_volume_size = 0;
    while( true )		// encode one member per iteration
      {
      const unsigned long long size = ( volume_size > 0 ) ?
        std::min( member_size, volume_size - partial_volume_size ) : member_size;
      show_progress( in_size, encoder, &pp, cfile_size );	// init
      if( !encoder->encode_member( size ) )
        { pp( "Encoder error." ); retval = 1; break; }
      in_size += encoder->data_position();
      out_size += encoder->member_position();
      if( encoder->data_finished() ) break;
      if( volume_size > 0 )
        {
        partial_volume_size += encoder->member_position();
        if( partial_volume_size >= volume_size - min_dictionary_size )
          {
          partial_volume_size = 0;
          if( delete_output_on_interrupt )
            {
            close_and_set_permissions( in_statsp );
            if( !next_filename() )
              { pp( "Too many volume files." ); retval = 1; break; }
            if( !open_outstream( true, !in_statsp ) ) { retval = 1; break; }
            }
          }
        }
      encoder->reset();
      }

    if( retval == 0 && verbosity >= 1 )
      {
      if( in_size == 0 || out_size == 0 )
        std::fputs( " no data compressed.\n", stderr );
      else
        std::fprintf( stderr, "%6.3f:1, %6.3f bits/byte, "
                              "%5.2f%% saved, %llu in, %llu out.\n",
                      (double)in_size / out_size,
                      ( 8.0 * out_size ) / in_size,
                      100.0 * ( 1.0 - ( (double)out_size / in_size ) ),
                      in_size, out_size );
      }
    }
  catch( std::bad_alloc )
    {
    pp( "Not enough memory. Try a smaller dictionary size." );
    retval = 1;
    }
  catch( Error e ) { pp(); show_error( e.msg, errno ); retval = 1; }
  delete encoder;
  return retval;
  }


unsigned char xdigit( const unsigned value )
  {
  if( value <= 9 ) return '0' + value;
  if( value <= 15 ) return 'A' + value - 10;
  return 0;
  }


bool show_trailing_data( const uint8_t * const data, const int size,
                         const Pretty_print & pp, const bool all,
                         const bool ignore_trailing )
  {
  if( verbosity >= 4 || !ignore_trailing )
    {
    std::string msg;
    if( !all ) msg = "first bytes of ";
    msg += "trailing data = ";
    for( int i = 0; i < size; ++i )
      {
      msg += xdigit( data[i] >> 4 );
      msg += xdigit( data[i] & 0x0F );
      msg += ' ';
      }
    msg += '\'';
    for( int i = 0; i < size; ++i )
      { if( std::isprint( data[i] ) ) msg += data[i]; else msg += '.'; }
    msg += '\'';
    pp( msg.c_str() );
    if( !ignore_trailing ) show_file_error( pp.name(), trailing_msg );
    }
  return ignore_trailing;
  }


int decompress( const int infd, const Pretty_print & pp,
                const bool ignore_trailing, const bool testing )
  {
  int retval = 0;

  try {
    unsigned long long partial_file_pos = 0;
    Range_decoder rdec( infd );
    for( bool first_member = true; ; first_member = false )
      {
      File_header header;
      rdec.reset_member_position();
      const int size = rdec.read_data( header.data, File_header::size );
      if( rdec.finished() )			// End Of File
        {
        if( first_member || header.verify_prefix( size ) )
          { pp( "File ends unexpectedly at member header." ); retval = 2; }
        else if( size > 0 && !show_trailing_data( header.data, size, pp,
                                                  true, ignore_trailing ) )
          retval = 2;
        break;
        }
      if( !header.verify_magic() )
        {
        if( first_member )
          { show_file_error( pp.name(), bad_magic_msg ); retval = 2; }
        else if( !show_trailing_data( header.data, size, pp, false, ignore_trailing ) )
          retval = 2;
        break;
        }
      if( !header.verify_version() )
        { pp( bad_version( header.version() ) ); retval = 2; break; }
      const unsigned dictionary_size = header.dictionary_size();
      if( !isvalid_ds( dictionary_size ) )
        { pp( bad_dict_msg ); retval = 2; break; }

      if( verbosity >= 2 || ( verbosity == 1 && first_member ) )
        { pp(); show_header( dictionary_size ); }

      LZ_decoder decoder( rdec, dictionary_size, outfd );
      const int result = decoder.decode_member( pp );
      partial_file_pos += rdec.member_position();
      if( result != 0 )
        {
        if( verbosity >= 0 && result <= 2 )
          {
          pp();
          std::fprintf( stderr, "%s at pos %llu\n", ( result == 2 ) ?
                        "File ends unexpectedly" : "Decoder error",
                        partial_file_pos );
          }
        retval = 2; break;
        }
      if( verbosity >= 2 )
        { std::fputs( testing ? "ok\n" : "done\n", stderr ); pp.reset(); }
      }
    }
  catch( std::bad_alloc ) { pp( "Not enough memory." ); retval = 1; }
  catch( Error e ) { pp(); show_error( e.msg, errno ); retval = 1; }
  if( verbosity == 1 && retval == 0 )
    std::fputs( testing ? "ok\n" : "done\n", stderr );
  return retval;
  }


extern "C" void signal_handler( int )
  {
  show_error( "Control-C or similar caught, quitting." );
  cleanup_and_fail( 1 );
  }


void set_signals()
  {
  std::signal( SIGHUP, signal_handler );
  std::signal( SIGINT, signal_handler );
  std::signal( SIGTERM, signal_handler );
  }

} // end namespace


void show_error( const char * const msg, const int errcode, const bool help )
  {
  if( verbosity < 0 ) return;
  if( msg && msg[0] )
    {
    std::fprintf( stderr, "%s: %s", program_name, msg );
    if( errcode > 0 ) std::fprintf( stderr, ": %s", std::strerror( errcode ) );
    std::fputc( '\n', stderr );
    }
  if( help )
    std::fprintf( stderr, "Try '%s --help' for more information.\n",
                  invocation_name );
  }


void show_file_error( const char * const filename, const char * const msg,
                      const int errcode )
  {
  if( verbosity < 0 ) return;
  std::fprintf( stderr, "%s: %s: %s", program_name, filename, msg );
  if( errcode > 0 ) std::fprintf( stderr, ": %s", std::strerror( errcode ) );
  std::fputc( '\n', stderr );
  }


void internal_error( const char * const msg )
  {
  if( verbosity >= 0 )
    std::fprintf( stderr, "%s: internal error: %s\n", program_name, msg );
  std::exit( 3 );
  }


void show_progress( const unsigned long long partial_size,
                    const Matchfinder_base * const m,
                    const Pretty_print * const p,
                    const unsigned long long cfile_size )
  {
  static unsigned long long csize = 0;		// file_size / 100
  static unsigned long long psize = 0;
  static const Matchfinder_base * mb = 0;
  static const Pretty_print * pp = 0;
      
  if ( kverbosity )
    {
    if( m )					// initialize static vars
    { csize = cfile_size; psize = partial_size; mb = m; }
      if( mb )
      {
        const unsigned long long pos = psize + mb->data_position();
        if( csize > 0 )
          {
          std::printf("%4llu%%", pos / csize);
          std::fflush(stdout);
          }
      }
    }

  if( verbosity < 2 ) return;
  if( m )					// initialize static vars
    { csize = cfile_size; psize = partial_size; mb = m; pp = p; }
  if( mb && pp )
    {
    const unsigned long long pos = psize + mb->data_position();
    if( csize > 0 )
      std::fprintf( stderr, "%4llu%%", pos / csize );
    std::fprintf( stderr, "  %.1f MB\r", pos / 1000000.0 );
    pp->reset(); (*pp)();			// restore cursor position
    }
  }


int main( const int argc, const char * const argv[] )
  {
  /* Mapping from gzip/bzip2 style 1..9 compression modes
     to the corresponding LZMA compression modes. */
  const Lzma_options option_mapping[] =
    {
    { 1 << 16,  16 },		// -0 entry values not used
    { 1 << 20,   5 },		// -1
    { 3 << 19,   6 },		// -2
    { 1 << 21,   8 },		// -3
    { 3 << 20,  12 },		// -4
    { 1 << 22,  20 },		// -5
    { 1 << 23,  36 },		// -6
    { 1 << 24,  68 },		// -7
    { 3 << 23, 132 },		// -8
    { 1 << 25, 273 } };		// -9
  Lzma_options encoder_options = option_mapping[6];	// default = "-6"
  const unsigned long long max_member_size = 0x0008000000000000ULL;
  const unsigned long long max_volume_size = 0x4000000000000000ULL;
  unsigned long long member_size = max_member_size;
  unsigned long long volume_size = 0;
  std::string default_output_filename;
  std::vector< std::string > filenames;
  int infd = -1;
  Mode program_mode = m_compress;
  bool force = false;
  bool ignore_trailing = true;
  bool keep_input_files = false;
  bool recompress = false;
  bool to_stdout = false;
  bool zero = false;
  invocation_name = argv[0];

  const Arg_parser::Option options[] =
    {
    { '0', "fast",            Arg_parser::no  },
    { '1',  0,                Arg_parser::no  },
    { '2',  0,                Arg_parser::no  },
    { '3',  0,                Arg_parser::no  },
    { '4',  0,                Arg_parser::no  },
    { '5',  0,                Arg_parser::no  },
    { '6',  0,                Arg_parser::no  },
    { '7',  0,                Arg_parser::no  },
    { '8',  0,                Arg_parser::no  },
    { '9', "best",            Arg_parser::no  },
    { 'a', "trailing-error",  Arg_parser::no  },
    { 'b', "member-size",     Arg_parser::yes },
    { 'c', "stdout",          Arg_parser::no  },
    { 'd', "decompress",      Arg_parser::no  },
    { 'f', "force",           Arg_parser::no  },
    { 'F', "recompress",      Arg_parser::no  },
    { 'h', "help",            Arg_parser::no  },
    { 'k', "keep",            Arg_parser::no  },
    { 'l', "list",            Arg_parser::no  },
    { 'm', "match-length",    Arg_parser::yes },
    { 'n', "threads",         Arg_parser::yes },
    { 'o', "output",          Arg_parser::yes },
    { 'O', "output-write",    Arg_parser::yes },
    { 'q', "quiet",           Arg_parser::no  },
    { 's', "dictionary-size", Arg_parser::yes },
    { 'S', "volume-size",     Arg_parser::yes },
    { 't', "test",            Arg_parser::no  },
    { 'v', "verbose",         Arg_parser::no  },
    { 'K', "keka-verbose",    Arg_parser::no  },
    { 'V', "version",         Arg_parser::no  },
    {  0 , 0,                 Arg_parser::no  } };

  const Arg_parser parser( argc, argv, options );
  if( parser.error().size() )				// bad option
    { show_error( parser.error().c_str(), 0, true ); return 1; }

  int argind = 0;
  for( ; argind < parser.arguments(); ++argind )
    {
    const int code = parser.code( argind );
    if( !code ) break;					// no more options
    const std::string & sarg = parser.argument( argind );
    const char * const arg = sarg.c_str();
    switch( code )
      {
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9':
                zero = ( code == '0' );
                encoder_options = option_mapping[code-'0']; break;
      case 'a': ignore_trailing = false; break;
      case 'b': member_size = getnum( arg, 100000, max_member_size ); break;
      case 'c': to_stdout = true; break;
      case 'd': set_mode( program_mode, m_decompress ); break;
      case 'f': force = true; break;
      case 'F': recompress = true; break;
      case 'h': show_help(); return 0;
      case 'k': keep_input_files = true; break;
      case 'l': set_mode( program_mode, m_list ); break;
      case 'm': encoder_options.match_len_limit =
                  getnum( arg, min_match_len_limit, max_match_len );
                zero = false; break;
      case 'n': break;
      case 'o': default_output_filename = sarg; break;
      case 'O': output_write = sarg; break;
      case 'q': verbosity = -1; break;
      case 's': encoder_options.dictionary_size = get_dict_size( arg );
                zero = false; break;
      case 'S': volume_size = getnum( arg, 100000, max_volume_size ); break;
      case 't': set_mode( program_mode, m_test ); break;
      case 'v': if( verbosity < 4 ) ++verbosity; break;
      case 'K': kverbosity = true; break;
      case 'V': show_version(); return 0;
      default : internal_error( "uncaught option." );
      }
    } // end process options

#if defined(__MSVCRT__) || defined(__OS2__)
  setmode( STDIN_FILENO, O_BINARY );
  setmode( STDOUT_FILENO, O_BINARY );
#endif

  bool filenames_given = false;
  for( ; argind < parser.arguments(); ++argind )
    {
    filenames.push_back( parser.argument( argind ) );
    if( filenames.back() != "-" ) filenames_given = true;
    }
  if( filenames.empty() ) filenames.push_back("-");

  if( program_mode == m_list )
    return list_files( filenames, ignore_trailing );

  if( program_mode == m_test )
    outfd = -1;
  else if( program_mode == m_compress )
    {
    dis_slots.init();
    prob_prices.init();
    }

  if( !to_stdout && program_mode != m_test &&
      ( filenames_given || default_output_filename.size() ) )
    set_signals();

  Pretty_print pp( filenames, verbosity );

  int retval = 0;
  bool stdin_used = false;
  for( unsigned i = 0; i < filenames.size(); ++i )
    {
    std::string input_filename;
    struct stat in_stats;
    output_filename.clear();

    if( filenames[i].empty() || filenames[i] == "-" )
      {
      if( stdin_used ) continue; else stdin_used = true;
      infd = STDIN_FILENO;
      if( program_mode != m_test )
        {
        if( to_stdout || default_output_filename.empty() )
          outfd = STDOUT_FILENO;
        else
          {
          if( program_mode == m_compress )
            set_c_outname( default_output_filename, volume_size > 0 );
          else output_filename = default_output_filename;
          if( !open_outstream( force, true ) )
            {
            if( retval < 1 ) retval = 1;
            close( infd ); infd = -1;
            continue;
            }
          }
        }
      }
    else
      {
      const int eindex = extension_index( input_filename = filenames[i] );
      infd = open_instream2( input_filename.c_str(), &in_stats, program_mode,
                             eindex, recompress, to_stdout );
      if( infd < 0 ) { if( retval < 1 ) retval = 1; continue; }
      if( program_mode != m_test )
        {
        if( to_stdout ) outfd = STDOUT_FILENO;
        else
          {
          if( program_mode == m_compress )
            set_c_outname( input_filename, volume_size > 0 );
          else set_d_outname( input_filename, eindex );
          if( !open_outstream( force, false ) )
            {
            if( retval < 1 ) retval = 1;
            close( infd ); infd = -1;
            continue;
            }
          }
        }
      }

    pp.set_name( input_filename );
    if( !check_tty( pp.name(), infd, program_mode ) )
      {
      if( retval < 1 ) retval = 1;
      if( program_mode == m_test ) { close( infd ); infd = -1; continue; }
      cleanup_and_fail( retval );
      }

    const struct stat * const in_statsp = input_filename.size() ? &in_stats : 0;
    int tmp;
    if( program_mode == m_compress )
      tmp = compress( member_size, volume_size, infd, encoder_options, pp,
                      in_statsp, zero );
    else
      tmp = decompress( infd, pp, ignore_trailing, program_mode == m_test );
    if( tmp > retval ) retval = tmp;
    if( tmp && program_mode != m_test ) cleanup_and_fail( retval );

    if( delete_output_on_interrupt )
      close_and_set_permissions( in_statsp );
    if( input_filename.size() )
      {
      close( infd ); infd = -1;
      if( !keep_input_files && !to_stdout && program_mode != m_test )
        std::remove( input_filename.c_str() );
      }
    }
  if( outfd >= 0 && close( outfd ) != 0 )
    {
    show_error( "Can't close stdout", errno );
    if( retval < 1 ) retval = 1;
    }
  return retval;
  }
