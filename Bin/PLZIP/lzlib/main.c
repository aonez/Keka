/*  Minilzip - Test program for the lzlib library
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
/*
    Exit status: 0 for a normal exit, 1 for environmental problems
    (file not found, invalid flags, I/O errors, etc), 2 to indicate a
    corrupt or invalid input file, 3 for an internal consistency error
    (eg, bug) which caused minilzip to panic.
*/

#define _FILE_OFFSET_BITS 64

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utime.h>
#include <sys/stat.h>
#if defined(__MSVCRT__)
#include <io.h>
#define fchmod(x,y) 0
#define fchown(x,y,z) 0
#define strtoull strtoul
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

#include "carg_parser.h"
#include "lzlib.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

#if CHAR_BIT != 8
#error "Environments where CHAR_BIT != 8 are not supported."
#endif

#ifndef max
  #define max(x,y) ((x) >= (y) ? (x) : (y))
#endif
#ifndef min
  #define min(x,y) ((x) <= (y) ? (x) : (y))
#endif

void cleanup_and_fail( const int retval );
void show_error( const char * const msg, const int errcode, const bool help );
void show_file_error( const char * const filename, const char * const msg,
                      const int errcode );
void internal_error( const char * const msg );

int verbosity = 0;

const char * const Program_name = "Minilzip";
const char * const program_name = "minilzip";
const char * const program_year = "2017";
const char * invocation_name = 0;

const struct { const char * from; const char * to; } known_extensions[] = {
  { ".lz",  ""     },
  { ".tlz", ".tar" },
  { 0,      0      } };

struct Lzma_options
  {
  int dictionary_size;		/* 4 KiB .. 512 MiB */
  int match_len_limit;		/* 5 .. 273 */
  };

enum Mode { m_compress, m_decompress, m_test };

char * output_filename = 0;
int outfd = -1;
bool delete_output_on_interrupt = false;


struct Pretty_print
  {
  const char * name;
  const char * stdin_name;
  unsigned longest_name;
  bool first_post;
  };

static void Pp_init( struct Pretty_print * const pp,
                     const char * const filenames[],
                     const int num_filenames, const int verbosity )
  {
  unsigned stdin_name_len;
  int i;
  pp->name = 0;
  pp->stdin_name = "(stdin)";
  pp->longest_name = 0;
  pp->first_post = false;

  if( verbosity <= 0 ) return;
  stdin_name_len = strlen( pp->stdin_name );
  for( i = 0; i < num_filenames; ++i )
    {
    const char * const s = filenames[i];
    const unsigned len = (strcmp( s, "-" ) == 0) ? stdin_name_len : strlen( s );
    if( len > pp->longest_name ) pp->longest_name = len;
    }
  if( pp->longest_name == 0 ) pp->longest_name = stdin_name_len;
  }

static inline void Pp_set_name( struct Pretty_print * const pp,
                                const char * const filename )
  {
  if( filename && filename[0] && strcmp( filename, "-" ) != 0 )
    pp->name = filename;
  else pp->name = pp->stdin_name;
  pp->first_post = true;
  }

static inline void Pp_reset( struct Pretty_print * const pp )
  { if( pp->name && pp->name[0] ) pp->first_post = true; }

static void Pp_show_msg( struct Pretty_print * const pp, const char * const msg )
  {
  if( verbosity >= 0 )
    {
    if( pp->first_post )
      {
      unsigned i;
      pp->first_post = false;
      fprintf( stderr, "  %s: ", pp->name );
      for( i = strlen( pp->name ); i < pp->longest_name; ++i )
        fputc( ' ', stderr );
      if( !msg ) fflush( stderr );
      }
    if( msg ) fprintf( stderr, "%s\n", msg );
    }
  }


static void show_help( void )
  {
  printf( "%s - Test program for the lzlib library.\n", Program_name );
  printf( "\nUsage: %s [options] [files]\n", invocation_name );
  printf( "\nOptions:\n"
          "  -h, --help                     display this help and exit\n"
          "  -V, --version                  output version information and exit\n"
          "  -a, --trailing-error           exit with error status if trailing data\n"
          "  -b, --member-size=<bytes>      set member size limit in bytes\n"
          "  -c, --stdout                   write to standard output, keep input files\n"
          "  -d, --decompress               decompress\n"
          "  -f, --force                    overwrite existing output files\n"
          "  -F, --recompress               force re-compression of compressed files\n"
          "  -k, --keep                     keep (don't delete) input files\n"
          "  -m, --match-length=<bytes>     set match length limit in bytes [36]\n"
          "  -o, --output=<file>            if reading standard input, write to <file>\n"
          "  -q, --quiet                    suppress all messages\n"
          "  -s, --dictionary-size=<bytes>  set dictionary size limit in bytes [8 MiB]\n"
          "  -S, --volume-size=<bytes>      set volume size limit in bytes\n"
          "  -t, --test                     test compressed file integrity\n"
          "  -v, --verbose                  be verbose (a 2nd -v gives more)\n"
          "  -0 .. -9                       set compression level [default 6]\n"
          "      --fast                     alias for -0\n"
          "      --best                     alias for -9\n"
          "If no file names are given, or if a file is '-', minilzip compresses or\n"
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
          "caused minilzip to panic.\n"
          "\nReport bugs to lzip-bug@nongnu.org\n"
          "Lzlib home page: http://www.nongnu.org/lzip/lzlib.html\n" );
  }


static void show_version( void )
  {
  printf( "%s %s\n", program_name, PROGVERSION );
  printf( "Copyright (C) %s Antonio Diaz Diaz.\n", program_year );
  printf( "Using lzlib %s\n", LZ_version() );
  printf( "License GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl.html>\n"
          "This is free software: you are free to change and redistribute it.\n"
          "There is NO WARRANTY, to the extent permitted by law.\n" );
  }


static void show_header( const unsigned dictionary_size )
  {
  if( verbosity >= 3 )
    {
    enum { factor = 1024 };
    const char * const prefix[8] =
      { "Ki", "Mi", "Gi", "Ti", "Pi", "Ei", "Zi", "Yi" };
    const char * p = "";
    const char * np = "  ";
    unsigned num = dictionary_size, i;
    bool exact = ( num % factor == 0 );

    for( i = 0; i < 8 && ( num > 9999 || ( exact && num >= factor ) ); ++i )
      { num /= factor; if( num % factor != 0 ) exact = false;
        p = prefix[i]; np = ""; }
    fprintf( stderr, "dictionary %s%4u %sB.  ", np, num, p );
    }
  }


static unsigned long long getnum( const char * const ptr,
                                  const unsigned long long llimit,
                                  const unsigned long long ulimit )
  {
  unsigned long long result;
  char * tail;
  errno = 0;
  result = strtoull( ptr, &tail, 0 );
  if( tail == ptr )
    {
    show_error( "Bad or missing numerical argument.", 0, true );
    exit( 1 );
    }

  if( !errno && tail[0] )
    {
    const unsigned factor = ( tail[1] == 'i' ) ? 1024 : 1000;
    int exponent = 0;				/* 0 = bad multiplier */
    int i;
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
      exit( 1 );
      }
    for( i = 0; i < exponent; ++i )
      {
      if( ulimit / factor >= result ) result *= factor;
      else { errno = ERANGE; break; }
      }
    }
  if( !errno && ( result < llimit || result > ulimit ) ) errno = ERANGE;
  if( errno )
    {
    show_error( "Numerical argument out of limits.", 0, false );
    exit( 1 );
    }
  return result;
  }


static int get_dict_size( const char * const arg )
  {
  char * tail;
  int dictionary_size;
  const long bits = strtol( arg, &tail, 0 );
  if( bits >= LZ_min_dictionary_bits() &&
      bits <= LZ_max_dictionary_bits() && *tail == 0 )
    return ( 1 << bits );
  dictionary_size = getnum( arg, LZ_min_dictionary_size(),
                                 LZ_max_dictionary_size() );
  if( dictionary_size == 65535 ) ++dictionary_size;	/* no fast encoder */
  return dictionary_size;
  }


static int extension_index( const char * const name )
  {
  int eindex;
  for( eindex = 0; known_extensions[eindex].from; ++eindex )
    {
    const char * const ext = known_extensions[eindex].from;
    const unsigned name_len = strlen( name );
    const unsigned ext_len = strlen( ext );
    if( name_len > ext_len &&
        strncmp( name + name_len - ext_len, ext, ext_len ) == 0 )
      return eindex;
    }
  return -1;
  }


static int open_instream( const char * const name, struct stat * const in_statsp,
                          const enum Mode program_mode, const int eindex,
                          const bool recompress, const bool to_stdout )
  {
  int infd = -1;
  if( program_mode == m_compress && !recompress && eindex >= 0 )
    {
    if( verbosity >= 0 )
      fprintf( stderr, "%s: Input file '%s' already has '%s' suffix.\n",
               program_name, name, known_extensions[eindex].from );
    }
  else
    {
    infd = open( name, O_RDONLY | O_BINARY );
    if( infd < 0 )
      show_file_error( name, "Can't open input file", errno );
    else
      {
      const int i = fstat( infd, in_statsp );
      const mode_t mode = in_statsp->st_mode;
      const bool can_read = ( i == 0 &&
                              ( S_ISBLK( mode ) || S_ISCHR( mode ) ||
                                S_ISFIFO( mode ) || S_ISSOCK( mode ) ) );
      const bool no_ofile = ( to_stdout || program_mode == m_test );
      if( i != 0 || ( !S_ISREG( mode ) && ( !can_read || !no_ofile ) ) )
        {
        if( verbosity >= 0 )
          fprintf( stderr, "%s: Input file '%s' is not a regular file%s.\n",
                   program_name, name,
                   ( can_read && !no_ofile ) ?
                   ",\n          and '--stdout' was not specified" : "" );
        close( infd );
        infd = -1;
        }
      }
    }
  return infd;
  }


/* assure at least a minimum size for buffer 'buf' */
static void * resize_buffer( void * buf, const unsigned min_size )
  {
  if( buf ) buf = realloc( buf, min_size );
  else buf = malloc( min_size );
  if( !buf )
    {
    show_error( "Not enough memory.", 0, false );
    cleanup_and_fail( 1 );
    }
  return buf;
  }


static void set_c_outname( const char * const name, const bool multifile )
  {
  output_filename = resize_buffer( output_filename, strlen( name ) + 5 +
                                   strlen( known_extensions[0].from ) + 1 );
  strcpy( output_filename, name );
  if( multifile ) strcat( output_filename, "00001" );
  strcat( output_filename, known_extensions[0].from );
  }


static void set_d_outname( const char * const name, const int eindex )
  {
  const unsigned name_len = strlen( name );
  if( eindex >= 0 )
    {
    const char * const from = known_extensions[eindex].from;
    const unsigned from_len = strlen( from );
    if( name_len > from_len )
      {
      output_filename = resize_buffer( output_filename, name_len +
                                       strlen( known_extensions[eindex].to ) + 1 );
      strcpy( output_filename, name );
      strcpy( output_filename + name_len - from_len, known_extensions[eindex].to );
      return;
      }
    }
  output_filename = resize_buffer( output_filename, name_len + 4 + 1 );
  strcpy( output_filename, name );
  strcat( output_filename, ".out" );
  if( verbosity >= 1 )
    fprintf( stderr, "%s: Can't guess original name for '%s' -- using '%s'\n",
             program_name, name, output_filename );
  }


static bool open_outstream( const bool force, const bool from_stdin )
  {
  const mode_t usr_rw = S_IRUSR | S_IWUSR;
  const mode_t all_rw = usr_rw | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
  const mode_t outfd_mode = from_stdin ? all_rw : usr_rw;
  int flags = O_CREAT | O_WRONLY | O_BINARY;
  if( force ) flags |= O_TRUNC; else flags |= O_EXCL;

  outfd = open( output_filename, flags, outfd_mode );
  if( outfd >= 0 ) delete_output_on_interrupt = true;
  else if( verbosity >= 0 )
    {
    if( errno == EEXIST )
      fprintf( stderr, "%s: Output file '%s' already exists, skipping.\n",
               program_name, output_filename );
    else
      fprintf( stderr, "%s: Can't create output file '%s': %s\n",
               program_name, output_filename, strerror( errno ) );
    }
  return ( outfd >= 0 );
  }


static bool check_tty( const char * const input_filename, const int infd,
                       const enum Mode program_mode )
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
                     "I won't read compressed data from a terminal.", 0 );
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
      fprintf( stderr, "%s: Deleting output file '%s', if it exists.\n",
               program_name, output_filename );
    if( outfd >= 0 ) { close( outfd ); outfd = -1; }
    if( remove( output_filename ) != 0 && errno != ENOENT )
      show_error( "WARNING: deletion of output file (apparently) failed.", 0, false );
    }
  exit( retval );
  }


     /* Set permissions, owner and times. */
static void close_and_set_permissions( const struct stat * const in_statsp )
  {
  bool warning = false;
  if( in_statsp )
    {
    const mode_t mode = in_statsp->st_mode;
    /* fchown will in many cases return with EPERM, which can be safely ignored. */
    if( fchown( outfd, in_statsp->st_uid, in_statsp->st_gid ) == 0 )
      { if( fchmod( outfd, mode ) != 0 ) warning = true; }
    else
      if( errno != EPERM ||
          fchmod( outfd, mode & ~( S_ISUID | S_ISGID | S_ISVTX ) ) != 0 )
        warning = true;
    }
  if( close( outfd ) != 0 )
    {
    show_error( "Error closing output file", errno, false );
    cleanup_and_fail( 1 );
    }
  outfd = -1;
  delete_output_on_interrupt = false;
  if( in_statsp )
    {
    struct utimbuf t;
    t.actime = in_statsp->st_atime;
    t.modtime = in_statsp->st_mtime;
    if( utime( output_filename, &t ) != 0 ) warning = true;
    }
  if( warning && verbosity >= 1 )
    show_error( "Can't change output file attributes.", 0, false );
  }


/* Returns the number of bytes really read.
   If (returned value < size) and (errno == 0), means EOF was reached.
*/
static int readblock( const int fd, uint8_t * const buf, const int size )
  {
  int sz = 0;
  errno = 0;
  while( sz < size )
    {
    const int n = read( fd, buf + sz, size - sz );
    if( n > 0 ) sz += n;
    else if( n == 0 ) break;				/* EOF */
    else if( errno != EINTR ) break;
    errno = 0;
    }
  return sz;
  }


/* Returns the number of bytes really written.
   If (returned value < size), it is always an error.
*/
static int writeblock( const int fd, const uint8_t * const buf, const int size )
  {
  int sz = 0;
  errno = 0;
  while( sz < size )
    {
    const int n = write( fd, buf + sz, size - sz );
    if( n > 0 ) sz += n;
    else if( n < 0 && errno != EINTR ) break;
    errno = 0;
    }
  return sz;
  }


static bool next_filename( void )
  {
  const unsigned name_len = strlen( output_filename );
  const unsigned ext_len = strlen( known_extensions[0].from );
  int i, j;
  if( name_len >= ext_len + 5 )				/* "*00001.lz" */
    for( i = name_len - ext_len - 1, j = 0; j < 5; --i, ++j )
      {
      if( output_filename[i] < '9' ) { ++output_filename[i]; return true; }
      else output_filename[i] = '0';
      }
  return false;
  }


static int do_compress( struct LZ_Encoder * const encoder,
                        const unsigned long long member_size,
                        const unsigned long long volume_size,
                        const int infd, struct Pretty_print * const pp,
                        const struct stat * const in_statsp )
  {
  unsigned long long partial_volume_size = 0;
  enum { buffer_size = 65536 };
  uint8_t buffer[buffer_size];
  if( verbosity >= 1 ) Pp_show_msg( pp, 0 );

  while( true )
    {
    int in_size = 0, out_size;
    while( LZ_compress_write_size( encoder ) > 0 )
      {
      const int size = min( LZ_compress_write_size( encoder ), buffer_size );
      const int rd = readblock( infd, buffer, size );
      if( rd != size && errno )
        {
        Pp_show_msg( pp, 0 ); show_error( "Read error", errno, false );
        return 1;
        }
      if( rd > 0 && rd != LZ_compress_write( encoder, buffer, rd ) )
        internal_error( "library error (LZ_compress_write)." );
      if( rd < size ) LZ_compress_finish( encoder );
/*      else LZ_compress_sync_flush( encoder ); */
      in_size += rd;
      }
    out_size = LZ_compress_read( encoder, buffer, buffer_size );
    if( out_size < 0 )
      {
      Pp_show_msg( pp, 0 );
      if( verbosity >= 0 )
        fprintf( stderr, "%s: LZ_compress_read error: %s\n",
                 program_name, LZ_strerror( LZ_compress_errno( encoder ) ) );
      return 1;
      }
    else if( out_size > 0 )
      {
      const int wr = writeblock( outfd, buffer, out_size );
      if( wr != out_size )
        {
        Pp_show_msg( pp, 0 ); show_error( "Write error", errno, false );
        return 1;
        }
      }
    else if( in_size == 0 ) internal_error( "library error (LZ_compress_read)." );
    if( LZ_compress_member_finished( encoder ) )
      {
      unsigned long long size;
      if( LZ_compress_finished( encoder ) == 1 ) break;
      if( volume_size > 0 )
        {
        partial_volume_size += LZ_compress_member_position( encoder );
        if( partial_volume_size >= volume_size - LZ_min_dictionary_size() )
          {
          partial_volume_size = 0;
          if( delete_output_on_interrupt )
            {
            close_and_set_permissions( in_statsp );
            if( !next_filename() )
              { Pp_show_msg( pp, "Too many volume files." ); return 1; }
            if( !open_outstream( true, !in_statsp ) ) return 1;
            }
          }
        size = min( member_size, volume_size - partial_volume_size );
        }
      else
        size = member_size;
      if( LZ_compress_restart_member( encoder, size ) < 0 )
        {
        Pp_show_msg( pp, 0 );
        if( verbosity >= 0 )
          fprintf( stderr, "%s: LZ_compress_restart_member error: %s\n",
                   program_name, LZ_strerror( LZ_compress_errno( encoder ) ) );
        return 1;
        }
      }
    }

  if( verbosity >= 1 )
    {
    const unsigned long long in_size = LZ_compress_total_in_size( encoder );
    const unsigned long long out_size = LZ_compress_total_out_size( encoder );
    if( in_size == 0 || out_size == 0 )
      fputs( " no data compressed.\n", stderr );
    else
      fprintf( stderr, "%6.3f:1, %6.3f bits/byte, "
                       "%5.2f%% saved, %llu in, %llu out.\n",
               (double)in_size / out_size,
               ( 8.0 * out_size ) / in_size,
               100.0 * ( 1.0 - ( (double)out_size / in_size ) ),
               in_size, out_size );
    }
  return 0;
  }


static int compress( const unsigned long long member_size,
                     const unsigned long long volume_size, const int infd,
                     const struct Lzma_options * const encoder_options,
                     struct Pretty_print * const pp,
                     const struct stat * const in_statsp )
  {
  struct LZ_Encoder * const encoder =
    LZ_compress_open( encoder_options->dictionary_size,
                      encoder_options->match_len_limit, ( volume_size > 0 ) ?
                      min( member_size, volume_size ) : member_size );
  int retval;

  if( !encoder || LZ_compress_errno( encoder ) != LZ_ok )
    {
    if( !encoder || LZ_compress_errno( encoder ) == LZ_mem_error )
      Pp_show_msg( pp, "Not enough memory. Try a smaller dictionary size." );
    else
      internal_error( "invalid argument to encoder." );
    retval = 1;
    }
  else retval = do_compress( encoder, member_size, volume_size,
                             infd, pp, in_statsp );
  LZ_compress_close( encoder );
  return retval;
  }


static int do_decompress( struct LZ_Decoder * const decoder, const int infd,
                          struct Pretty_print * const pp,
                          const bool ignore_trailing, const bool testing )
  {
  enum { buffer_size = 65536 };
  uint8_t buffer[buffer_size];
  bool first_member;

  for( first_member = true; ; )
    {
    const int max_in_size = min( LZ_decompress_write_size( decoder ), buffer_size );
    int in_size = 0, out_size = 0;
    if( max_in_size > 0 )
      {
      in_size = readblock( infd, buffer, max_in_size );
      if( in_size != max_in_size && errno )
        {
        Pp_show_msg( pp, 0 ); show_error( "Read error", errno, false );
        return 1;
        }
      if( in_size > 0 && in_size != LZ_decompress_write( decoder, buffer, in_size ) )
        internal_error( "library error (LZ_decompress_write)." );
      if( in_size < max_in_size ) LZ_decompress_finish( decoder );
      }
    while( true )
      {
      const int rd = LZ_decompress_read( decoder, buffer, buffer_size );
      if( rd > 0 )
        {
        out_size += rd;
        if( outfd >= 0 )
          {
          const int wr = writeblock( outfd, buffer, rd );
          if( wr != rd )
            {
            Pp_show_msg( pp, 0 ); show_error( "Write error", errno, false );
            return 1;
            }
          }
        }
      else if( rd < 0 ) { out_size = rd; break; }
      if( LZ_decompress_member_finished( decoder ) == 1 )
        {
        if( verbosity >= 1 )
          {
          const unsigned long long data_size = LZ_decompress_data_position( decoder );
          const unsigned long long member_size = LZ_decompress_member_position( decoder );
          Pp_show_msg( pp, 0 );
          show_header( LZ_decompress_dictionary_size( decoder ) );
          if( verbosity >= 2 && data_size > 0 && member_size > 0 )
            fprintf( stderr, "%6.3f:1, %6.3f bits/byte, %5.2f%% saved.  ",
                     (double)data_size / member_size,
                     ( 8.0 * member_size ) / data_size,
                     100.0 * ( 1.0 - ( (double)member_size / data_size ) ) );
          if( verbosity >= 4 )
            fprintf( stderr, "CRC %08X, decompressed %9llu, compressed %8llu.  ",
                     LZ_decompress_data_crc( decoder ), data_size, member_size );
          fputs( testing ? "ok\n" : "done\n", stderr );
          }
        first_member = false; Pp_reset( pp );
        }
      if( rd <= 0 ) break;
      }
    if( out_size < 0 || ( first_member && out_size == 0 ) )
      {
      const enum LZ_Errno lz_errno = LZ_decompress_errno( decoder );
      if( lz_errno == LZ_unexpected_eof &&
          LZ_decompress_member_position( decoder ) <= 6 )
        { Pp_show_msg( pp, "File ends unexpectedly at member header." );
          return 2; }
      if( lz_errno == LZ_header_error )
        {
        if( first_member )
          { show_file_error( pp->name,
                             "Bad magic number (file not in lzip format).", 0 );
            return 2; }
        else if( !ignore_trailing )
          { show_file_error( pp->name, "Trailing data not allowed.", 0 );
            return 2; }
        break;
        }
      if( lz_errno == LZ_mem_error )
        { Pp_show_msg( pp, "Not enough memory." ); return 1; }
      if( verbosity >= 0 )
        {
        Pp_show_msg( pp, 0 );
        if( lz_errno == LZ_unexpected_eof )
          fprintf( stderr, "File ends unexpectedly at pos %llu\n",
                   LZ_decompress_total_in_size( decoder ) );
        else
          fprintf( stderr, "Decoder error at pos %llu: %s\n",
                   LZ_decompress_total_in_size( decoder ),
                   LZ_strerror( LZ_decompress_errno( decoder ) ) );
        }
      return 2;
      }
    if( LZ_decompress_finished( decoder ) == 1 ) break;
    if( in_size == 0 && out_size == 0 )
      internal_error( "library error (LZ_decompress_read)." );
    }
  return 0;
  }


static int decompress( const int infd, struct Pretty_print * const pp,
                       const bool ignore_trailing, const bool testing )
  {
  struct LZ_Decoder * const decoder = LZ_decompress_open();
  int retval;

  if( !decoder || LZ_decompress_errno( decoder ) != LZ_ok )
    { Pp_show_msg( pp, "Not enough memory." ); retval = 1; }
  else retval = do_decompress( decoder, infd, pp, ignore_trailing, testing );

  LZ_decompress_close( decoder );
  return retval;
  }


void signal_handler( int sig )
  {
  if( sig ) {}				/* keep compiler happy */
  show_error( "Control-C or similar caught, quitting.", 0, false );
  cleanup_and_fail( 1 );
  }


static void set_signals( void )
  {
  signal( SIGHUP, signal_handler );
  signal( SIGINT, signal_handler );
  signal( SIGTERM, signal_handler );
  }


void show_error( const char * const msg, const int errcode, const bool help )
  {
  if( verbosity < 0 ) return;
  if( msg && msg[0] )
    {
    fprintf( stderr, "%s: %s", program_name, msg );
    if( errcode > 0 ) fprintf( stderr, ": %s", strerror( errcode ) );
    fputc( '\n', stderr );
    }
  if( help )
    fprintf( stderr, "Try '%s --help' for more information.\n",
             invocation_name );
  }


void show_file_error( const char * const filename, const char * const msg,
                      const int errcode )
  {
  if( verbosity < 0 ) return;
  fprintf( stderr, "%s: %s: %s", program_name, filename, msg );
  if( errcode > 0 ) fprintf( stderr, ": %s", strerror( errcode ) );
  fputc( '\n', stderr );
  }


void internal_error( const char * const msg )
  {
  if( verbosity >= 0 )
    fprintf( stderr, "%s: internal error: %s\n", program_name, msg );
  exit( 3 );
  }


int main( const int argc, const char * const argv[] )
  {
  /* Mapping from gzip/bzip2 style 1..9 compression modes
     to the corresponding LZMA compression modes. */
  const struct Lzma_options option_mapping[] =
    {
    {   65535,  16 },		/* -0 (65535,16 chooses fast encoder) */
    { 1 << 20,   5 },		/* -1 */
    { 3 << 19,   6 },		/* -2 */
    { 1 << 21,   8 },		/* -3 */
    { 3 << 20,  12 },		/* -4 */
    { 1 << 22,  20 },		/* -5 */
    { 1 << 23,  36 },		/* -6 */
    { 1 << 24,  68 },		/* -7 */
    { 3 << 23, 132 },		/* -8 */
    { 1 << 25, 273 } };		/* -9 */
  struct Lzma_options encoder_options = option_mapping[6];  /* default = "-6" */
  const unsigned long long max_member_size = 0x0008000000000000ULL;
  const unsigned long long max_volume_size = 0x4000000000000000ULL;
  unsigned long long member_size = max_member_size;
  unsigned long long volume_size = 0;
  const char * default_output_filename = "";
  const char ** filenames = 0;
  int num_filenames = 0;
  int infd = -1;
  enum Mode program_mode = m_compress;
  int argind = 0;
  int retval = 0;
  int i;
  bool filenames_given = false;
  bool force = false;
  bool ignore_trailing = true;
  bool keep_input_files = false;
  bool recompress = false;
  bool stdin_used = false;
  bool to_stdout = false;
  struct Pretty_print pp;

  const struct ap_Option options[] =
    {
    { '0', "fast",            ap_no  },
    { '1',  0,                ap_no  },
    { '2',  0,                ap_no  },
    { '3',  0,                ap_no  },
    { '4',  0,                ap_no  },
    { '5',  0,                ap_no  },
    { '6',  0,                ap_no  },
    { '7',  0,                ap_no  },
    { '8',  0,                ap_no  },
    { '9', "best",            ap_no  },
    { 'a', "trailing-error",  ap_no  },
    { 'b', "member-size",     ap_yes },
    { 'c', "stdout",          ap_no  },
    { 'd', "decompress",      ap_no  },
    { 'f', "force",           ap_no  },
    { 'F', "recompress",      ap_no  },
    { 'h', "help",            ap_no  },
    { 'k', "keep",            ap_no  },
    { 'm', "match-length",    ap_yes },
    { 'n', "threads",         ap_yes },
    { 'o', "output",          ap_yes },
    { 'q', "quiet",           ap_no  },
    { 's', "dictionary-size", ap_yes },
    { 'S', "volume-size",     ap_yes },
    { 't', "test",            ap_no  },
    { 'v', "verbose",         ap_no  },
    { 'V', "version",         ap_no  },
    {  0 ,  0,                ap_no  } };

  struct Arg_parser parser;

  invocation_name = argv[0];

  if( LZ_version()[0] != LZ_version_string[0] )
    internal_error( "bad library version." );
  if( strcmp( PROGVERSION, LZ_version_string ) != 0 )
    internal_error( "bad library version_string." );

  if( !ap_init( &parser, argc, argv, options, 0 ) )
    { show_error( "Not enough memory.", 0, false ); return 1; }
  if( ap_error( &parser ) )				/* bad option */
    { show_error( ap_error( &parser ), 0, true ); return 1; }

  for( ; argind < ap_arguments( &parser ); ++argind )
    {
    const int code = ap_code( &parser, argind );
    const char * const arg = ap_argument( &parser, argind );
    if( !code ) break;					/* no more options */
    switch( code )
      {
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9':
                encoder_options = option_mapping[code-'0']; break;
      case 'a': ignore_trailing = false; break;
      case 'b': member_size = getnum( arg, 100000, max_member_size ); break;
      case 'c': to_stdout = true; break;
      case 'd': program_mode = m_decompress; break;
      case 'f': force = true; break;
      case 'F': recompress = true; break;
      case 'h': show_help(); return 0;
      case 'k': keep_input_files = true; break;
      case 'm': encoder_options.match_len_limit =
                  getnum( arg, LZ_min_match_len_limit(),
                               LZ_max_match_len_limit() ); break;
      case 'n': break;
      case 'o': default_output_filename = arg; break;
      case 'q': verbosity = -1; break;
      case 's': encoder_options.dictionary_size = get_dict_size( arg );
                break;
      case 'S': volume_size = getnum( arg, 100000, max_volume_size ); break;
      case 't': program_mode = m_test; break;
      case 'v': if( verbosity < 4 ) ++verbosity; break;
      case 'V': show_version(); return 0;
      default : internal_error( "uncaught option." );
      }
    } /* end process options */

#if defined(__MSVCRT__) || defined(__OS2__)
  setmode( STDIN_FILENO, O_BINARY );
  setmode( STDOUT_FILENO, O_BINARY );
#endif

  num_filenames = max( 1, ap_arguments( &parser ) - argind );
  filenames = resize_buffer( filenames, num_filenames * sizeof filenames[0] );
  filenames[0] = "-";

  for( i = 0; argind + i < ap_arguments( &parser ); ++i )
    {
    filenames[i] = ap_argument( &parser, argind + i );
    if( strcmp( filenames[i], "-" ) != 0 ) filenames_given = true;
    }

  if( program_mode == m_test )
    outfd = -1;

  if( !to_stdout && program_mode != m_test &&
      ( filenames_given || default_output_filename[0] ) )
    set_signals();

  Pp_init( &pp, filenames, num_filenames, verbosity );

  output_filename = resize_buffer( output_filename, 1 );
  for( i = 0; i < num_filenames; ++i )
    {
    const char * input_filename = "";
    int tmp;
    struct stat in_stats;
    const struct stat * in_statsp;
    output_filename[0] = 0;

    if( !filenames[i][0] || strcmp( filenames[i], "-" ) == 0 )
      {
      if( stdin_used ) continue; else stdin_used = true;
      infd = STDIN_FILENO;
      if( program_mode != m_test )
        {
        if( to_stdout || !default_output_filename[0] )
          outfd = STDOUT_FILENO;
        else
          {
          if( program_mode == m_compress )
            set_c_outname( default_output_filename, volume_size > 0 );
          else
            {
            output_filename = resize_buffer( output_filename,
                                             strlen( default_output_filename ) + 1 );
            strcpy( output_filename, default_output_filename );
            }
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
      infd = open_instream( input_filename, &in_stats, program_mode,
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

    Pp_set_name( &pp, input_filename );
    if( !check_tty( pp.name, infd, program_mode ) )
      {
      if( retval < 1 ) retval = 1;
      if( program_mode == m_test ) { close( infd ); infd = -1; continue; }
      cleanup_and_fail( retval );
      }

    in_statsp = input_filename[0] ? &in_stats : 0;
    if( program_mode == m_compress )
      tmp = compress( member_size, volume_size, infd, &encoder_options, &pp,
                      in_statsp );
    else
      tmp = decompress( infd, &pp, ignore_trailing, program_mode == m_test );
    if( tmp > retval ) retval = tmp;
    if( tmp && program_mode != m_test ) cleanup_and_fail( retval );

    if( delete_output_on_interrupt )
      close_and_set_permissions( in_statsp );
    if( input_filename[0] )
      {
      close( infd ); infd = -1;
      if( !keep_input_files && !to_stdout && program_mode != m_test )
        remove( input_filename );
      }
    }
  if( outfd >= 0 && close( outfd ) != 0 )
    {
    show_error( "Can't close stdout", errno, false );
    if( retval < 1 ) retval = 1;
    }
  free( output_filename );
  free( filenames );
  ap_free( &parser );
  return retval;
  }
