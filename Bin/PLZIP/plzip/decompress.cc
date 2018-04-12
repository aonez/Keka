/*  Plzip - Parallel compressor compatible with lzip
    Copyright (C) 2009 Laszlo Ersek.
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

#define _FILE_OFFSET_BITS 64

#include <algorithm>
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>
#include "lzlib.h" //#include <lzlib.h>

#include "lzip.h"
#include "file_index.h"


void Pretty_print::operator()( const char * const msg ) const
  {
  if( verbosity >= 0 )
    {
    if( first_post )
      {
      first_post = false;
      std::fprintf( stderr, "  %s: ", name_.c_str() );
      for( unsigned i = name_.size(); i < longest_name; ++i )
        std::fputc( ' ', stderr );
      if( !msg ) std::fflush( stderr );
      }
    if( msg ) std::fprintf( stderr, "%s\n", msg );
    }
  }


// Returns the number of bytes really read.
// If (returned value < size) and (errno == 0), means EOF was reached.
//
int preadblock( const int fd, uint8_t * const buf, const int size,
                const long long pos )
  {
  int sz = 0;
  errno = 0;
  while( sz < size )
    {
    const int n = pread( fd, buf + sz, size - sz, pos + sz );
    if( n > 0 ) sz += n;
    else if( n == 0 ) break;				// EOF
    else if( errno != EINTR ) break;
    errno = 0;
    }
  return sz;
  }


// Returns the number of bytes really written.
// If (returned value < size), it is always an error.
//
int pwriteblock( const int fd, const uint8_t * const buf, const int size,
                 const long long pos )
  {
  int sz = 0;
  errno = 0;
  while( sz < size )
    {
    const int n = pwrite( fd, buf + sz, size - sz, pos + sz );
    if( n > 0 ) sz += n;
    else if( n < 0 && errno != EINTR ) break;
    errno = 0;
    }
  return sz;
  }


int decompress_read_error( struct LZ_Decoder * const decoder,
                           const Pretty_print & pp, const int worker_id )
  {
  const LZ_Errno errcode = LZ_decompress_errno( decoder );
  pp();
  if( verbosity >= 0 )
    std::fprintf( stderr, "LZ_decompress_read error in worker %d: %s\n",
                  worker_id, LZ_strerror( errcode ) );
  if( errcode == LZ_header_error || errcode == LZ_unexpected_eof ||
      errcode == LZ_data_error )
    return 2;
  return 1;
  }


namespace {

struct Worker_arg
  {
  const File_index * file_index;
  const Pretty_print * pp;
  int worker_id;
  int num_workers;
  int infd;
  int outfd;
  };


       // read members from file, decompress their contents, and
       // write the produced data to file.
extern "C" void * dworker( void * arg )
  {
  const Worker_arg & tmp = *(Worker_arg *)arg;
  const File_index & file_index = *tmp.file_index;
  const Pretty_print & pp = *tmp.pp;
  const int worker_id = tmp.worker_id;
  const int num_workers = tmp.num_workers;
  const int infd = tmp.infd;
  const int outfd = tmp.outfd;
  const int buffer_size = 65536;

  uint8_t * const ibuffer = new( std::nothrow ) uint8_t[buffer_size];
  uint8_t * const obuffer = new( std::nothrow ) uint8_t[buffer_size];
  LZ_Decoder * const decoder = LZ_decompress_open();
  if( !ibuffer || !obuffer || !decoder ||
      LZ_decompress_errno( decoder ) != LZ_ok )
    { pp( "Not enough memory." ); cleanup_and_fail(); }

  for( long i = worker_id; i < file_index.members(); i += num_workers )
    {
    long long data_pos = file_index.dblock( i ).pos();
    long long data_rest = file_index.dblock( i ).size();
    long long member_pos = file_index.mblock( i ).pos();
    long long member_rest = file_index.mblock( i ).size();

    while( member_rest > 0 )
      {
      while( LZ_decompress_write_size( decoder ) > 0 )
        {
        const int size = std::min( LZ_decompress_write_size( decoder ),
                    (int)std::min( (long long)buffer_size, member_rest ) );
        if( size > 0 )
          {
          if( preadblock( infd, ibuffer, size, member_pos ) != size )
            { pp(); show_error( "Read error", errno ); cleanup_and_fail(); }
          member_pos += size;
          member_rest -= size;
          if( LZ_decompress_write( decoder, ibuffer, size ) != size )
            internal_error( "library error (LZ_decompress_write)." );
          }
        if( member_rest <= 0 ) { LZ_decompress_finish( decoder ); break; }
        }
      while( true )			// write decompressed data to file
        {
        const int rd = LZ_decompress_read( decoder, obuffer, buffer_size );
        if( rd < 0 )
          cleanup_and_fail( decompress_read_error( decoder, pp, worker_id ) );
        if( rd > 0 && outfd >= 0 )
          {
          const int wr = pwriteblock( outfd, obuffer, rd, data_pos );
          if( wr != rd )
            {
            pp();
            if( verbosity >= 0 )
              std::fprintf( stderr, "Write error in worker %d: %s\n",
                            worker_id, std::strerror( errno ) );
            cleanup_and_fail();
            }
          }
        if( rd > 0 )
          {
          data_pos += rd;
          data_rest -= rd;
          }
        if( LZ_decompress_finished( decoder ) == 1 )
          {
          if( data_rest != 0 )
            internal_error( "final data_rest is not zero." );
          LZ_decompress_reset( decoder );	// prepare for new member
          break;
          }
        if( rd == 0 ) break;
        }
      }
    }

  delete[] obuffer; delete[] ibuffer;
  if( LZ_decompress_member_position( decoder ) != 0 )
    { pp( "Error, some data remains in decoder." ); cleanup_and_fail(); }
  if( LZ_decompress_close( decoder ) < 0 )
    { pp( "LZ_decompress_close failed." ); cleanup_and_fail(); }
  return 0;
  }

} // end namespace


    // start the workers and wait for them to finish.
int decompress( int num_workers, const int infd, const int outfd,
                const Pretty_print & pp, const int debug_level,
                const bool ignore_trailing, const bool infd_isreg )
  {
  if( !infd_isreg )
    return dec_stream( num_workers, infd, outfd, pp, debug_level, ignore_trailing );

  const File_index file_index( infd, ignore_trailing );
  if( file_index.retval() == 1 )
    {
    lseek( infd, 0, SEEK_SET );
    return dec_stream( num_workers, infd, outfd, pp, debug_level, ignore_trailing );
    }
  if( file_index.retval() != 0 )
    { pp( file_index.error().c_str() ); return file_index.retval(); }

  show_header( file_index.dictionary_size( 0 ) );
  if( num_workers > file_index.members() )
    num_workers = file_index.members();

  if( outfd >= 0 )
    {
    struct stat st;
    if( fstat( outfd, &st ) != 0 || !S_ISREG( st.st_mode ) ||
        lseek( outfd, 0, SEEK_CUR ) < 0 )
    return dec_stdout( num_workers, infd, outfd, pp, debug_level, file_index );
    }

  Worker_arg * worker_args = new( std::nothrow ) Worker_arg[num_workers];
  pthread_t * worker_threads = new( std::nothrow ) pthread_t[num_workers];
  if( !worker_args || !worker_threads )
    { pp( "Not enough memory." ); cleanup_and_fail(); }
  for( int i = 0; i < num_workers; ++i )
    {
    worker_args[i].file_index = &file_index;
    worker_args[i].pp = &pp;
    worker_args[i].worker_id = i;
    worker_args[i].num_workers = num_workers;
    worker_args[i].infd = infd;
    worker_args[i].outfd = outfd;
    const int errcode =
      pthread_create( &worker_threads[i], 0, dworker, &worker_args[i] );
    if( errcode )
      { show_error( "Can't create worker threads", errcode ); cleanup_and_fail(); }
    }

  for( int i = num_workers - 1; i >= 0; --i )
    {
    const int errcode = pthread_join( worker_threads[i], 0 );
    if( errcode )
      { show_error( "Can't join worker threads", errcode ); cleanup_and_fail(); }
    }
  delete[] worker_threads;
  delete[] worker_args;

  const unsigned long long in_size = file_index.cdata_size();
  const unsigned long long out_size = file_index.udata_size();
  if( verbosity >= 2 && out_size > 0 && in_size > 0 )
    std::fprintf( stderr, "%6.3f:1, %6.3f bits/byte, %5.2f%% saved.  ",
                  (double)out_size / in_size,
                  ( 8.0 * in_size ) / out_size,
                  100.0 * ( 1.0 - ( (double)in_size / out_size ) ) );
  if( verbosity >= 4 )
    std::fprintf( stderr, "decompressed %9llu, compressed %9llu.  ",
                  out_size, in_size );

  if( verbosity >= 1 ) std::fputs( (outfd < 0) ? "ok\n" : "done\n", stderr );

  return 0;
  }
