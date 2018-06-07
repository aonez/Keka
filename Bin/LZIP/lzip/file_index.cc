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

#define _FILE_OFFSET_BITS 64

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <stdint.h>
#include <unistd.h>

#include "lzip.h"
#include "file_index.h"


namespace {

int seek_read( const int fd, uint8_t * const buf, const int size,
               const long long pos )
  {
  if( lseek( fd, pos, SEEK_SET ) == pos )
    return readblock( fd, buf, size );
  return 0;
  }

} // end namespace


void File_index::set_errno_error( const char * const msg )
  {
  error_ = msg; error_ += std::strerror( errno );
  retval_ = 1;
  }

void File_index::set_num_error( const char * const msg, unsigned long long num )
  {
  char buf[80];
  snprintf( buf, sizeof buf, "%s%llu", msg, num );
  error_ = buf;
  retval_ = 2;
  }


// If successful, push last member and set pos to member header.
bool File_index::skip_trailing_data( const int fd, long long & pos )
  {
  enum { block_size = 16384,
         buffer_size = block_size + File_trailer::size - 1 + File_header::size };
  uint8_t buffer[buffer_size];
  if( pos < min_member_size ) return false;
  int bsize = pos % block_size;			// total bytes in buffer
  if( bsize <= buffer_size - block_size ) bsize += block_size;
  int search_size = bsize;			// bytes to search for trailer
  int rd_size = bsize;				// bytes to read from file
  unsigned long long ipos = pos - rd_size;	// aligned to block_size

  while( true )
    {
    if( seek_read( fd, buffer, rd_size, ipos ) != rd_size )
      { set_errno_error( "Error seeking member trailer: " ); return false; }
    const uint8_t max_msb = ( ipos + search_size ) >> 56;
    for( int i = search_size; i >= File_trailer::size; --i )
      if( buffer[i-1] <= max_msb )	// most significant byte of member_size
        {
        File_trailer & trailer =
          *(File_trailer *)( buffer + i - File_trailer::size );
        const unsigned long long member_size = trailer.member_size();
        if( member_size == 0 )
          { while( i > File_trailer::size && buffer[i-9] == 0 ) --i; continue; }
        if( member_size < min_member_size || member_size > ipos + i )
          continue;
        File_header header;
        if( seek_read( fd, header.data, File_header::size,
                       ipos + i - member_size ) != File_header::size )
          { set_errno_error( "Error reading member header: " ); return false; }
        const unsigned dictionary_size = header.dictionary_size();
        if( !header.verify_magic() || !header.verify_version() ||
            !isvalid_ds( dictionary_size ) ) continue;
        if( (*(File_header *)( buffer + i )).verify_prefix( bsize - i ) )
          {
          error_ = "Last member in input file is truncated or corrupt.";
          retval_ = 2; return false;
          }
        pos = ipos + i - member_size;
        member_vector.push_back( Member( 0, trailer.data_size(), pos,
                                         member_size, dictionary_size ) );
        return true;
        }
    if( ipos <= 0 )
      { set_num_error( "Member size in trailer is corrupt at pos ", pos - 8 );
        return false; }
    bsize = buffer_size;
    search_size = bsize - File_header::size;
    rd_size = block_size;
    ipos -= rd_size;
    std::memcpy( buffer + rd_size, buffer, buffer_size - rd_size );
    }
  }


File_index::File_index( const int infd, const bool ignore_trailing )
  : isize( lseek( infd, 0, SEEK_END ) ), retval_( 0 )
  {
  if( isize < 0 )
    { set_errno_error( "Input file is not seekable: " ); return; }
  if( isize < min_member_size )
    { error_ = "Input file is too short."; retval_ = 2; return; }
  if( isize > INT64_MAX )
    { error_ = "Input file is too long (2^63 bytes or more).";
      retval_ = 2; return; }

  File_header header;
  if( seek_read( infd, header.data, File_header::size, 0 ) != File_header::size )
    { set_errno_error( "Error reading member header: " ); return; }
  if( !header.verify_magic() )
    { error_ = bad_magic_msg; retval_ = 2; return; }
  if( !header.verify_version() )
    { error_ = bad_version( header.version() ); retval_ = 2; return; }
  if( !isvalid_ds( header.dictionary_size() ) )
    { error_ = bad_dict_msg; retval_ = 2; return; }

  long long pos = isize;	// always points to a header or to EOF
  while( pos >= min_member_size )
    {
    File_trailer trailer;
    if( seek_read( infd, trailer.data, File_trailer::size,
                   pos - File_trailer::size ) != File_trailer::size )
      { set_errno_error( "Error reading member trailer: " ); break; }
    const unsigned long long member_size = trailer.member_size();
    if( member_size < min_member_size || member_size > (unsigned long long)pos )
      {
      if( !member_vector.empty() )
        set_num_error( "Member size in trailer is corrupt at pos ", pos - 8 );
      else if( skip_trailing_data( infd, pos ) )
        { if( ignore_trailing ) continue;
          error_ = trailing_msg; retval_ = 2; return; }
      break;
      }
    if( seek_read( infd, header.data, File_header::size,
                   pos - member_size ) != File_header::size )
      { set_errno_error( "Error reading member header: " ); break; }
    const unsigned dictionary_size = header.dictionary_size();
    if( !header.verify_magic() || !header.verify_version() ||
        !isvalid_ds( dictionary_size ) )
      {
      if( !member_vector.empty() )
        set_num_error( "Bad header at pos ", pos - member_size );
      else if( skip_trailing_data( infd, pos ) )
        { if( ignore_trailing ) continue;
          error_ = trailing_msg; retval_ = 2; return; }
      break;
      }
    pos -= member_size;
    member_vector.push_back( Member( 0, trailer.data_size(), pos,
                                     member_size, dictionary_size ) );
    }
  if( pos != 0 || member_vector.empty() )
    {
    member_vector.clear();
    if( retval_ == 0 ) { error_ = "Can't create file index."; retval_ = 2; }
    return;
    }
  std::reverse( member_vector.begin(), member_vector.end() );
  for( unsigned long i = 0; i < member_vector.size() - 1; ++i )
    {
    const long long end = member_vector[i].dblock.end();
    if( end < 0 || end > INT64_MAX )
      {
      member_vector.clear();
      error_ = "Data in input file is too long (2^63 bytes or more).";
      retval_ = 2; return;
      }
    member_vector[i+1].dblock.pos( end );
    }
  }
