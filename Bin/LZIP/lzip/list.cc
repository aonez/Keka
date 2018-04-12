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

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>

#include "lzip.h"
#include "file_index.h"


namespace {

void list_line( const unsigned long long uncomp_size,
                const unsigned long long comp_size,
                const char * const input_filename )
  {
  if( uncomp_size > 0 )
    std::printf( "%15llu %15llu %6.2f%%  %s\n", uncomp_size, comp_size,
                  100.0 * ( 1.0 - ( (double)comp_size / uncomp_size ) ),
                  input_filename );
  else
    std::printf( "%15llu %15llu   -INF%%  %s\n", uncomp_size, comp_size,
                  input_filename );
  }

} // end namespace


int list_files( const std::vector< std::string > & filenames,
                const bool ignore_trailing )
  {
  unsigned long long total_comp = 0, total_uncomp = 0;
  int files = 0, retval = 0;
  bool first_post = true;
  bool stdin_used = false;
  for( unsigned i = 0; i < filenames.size(); ++i )
    {
    const bool from_stdin = ( filenames[i] == "-" );
    if( from_stdin ) { if( stdin_used ) continue; else stdin_used = true; }
    const char * const input_filename =
      from_stdin ? "(stdin)" : filenames[i].c_str();
    struct stat in_stats;				// not used
    const int infd = from_stdin ? STDIN_FILENO :
      open_instream( input_filename, &in_stats, true, true );
    if( infd < 0 ) { if( retval < 1 ) retval = 1; continue; }

    const File_index file_index( infd, ignore_trailing );
    close( infd );
    if( file_index.retval() != 0 )
      {
      show_file_error( input_filename, file_index.error().c_str() );
      if( retval < file_index.retval() ) retval = file_index.retval();
      continue;
      }
    if( verbosity >= 0 )
      {
      const unsigned long long udata_size = file_index.udata_size();
      const unsigned long long cdata_size = file_index.cdata_size();
      total_comp += cdata_size; total_uncomp += udata_size; ++files;
      if( first_post )
        {
        first_post = false;
        if( verbosity >= 1 ) std::fputs( "   dict   memb  trail ", stdout );
        std::fputs( "   uncompressed      compressed   saved  name\n", stdout );
        }
      if( verbosity >= 1 )
        {
        unsigned dictionary_size = 0;
        for( long i = 0; i < file_index.members(); ++i )
          dictionary_size =
            std::max( dictionary_size, file_index.dictionary_size( i ) );
        const long long trailing_size = file_index.file_size() - cdata_size;
        std::printf( "%s %5ld %6lld ", format_ds( dictionary_size ),
                     file_index.members(), trailing_size );
        }
      list_line( udata_size, cdata_size, input_filename );

      if( verbosity >= 2 && file_index.members() > 1 )
        {
        std::fputs( " member      data_pos       data_size      member_pos     member_size\n", stdout );
        for( long i = 0; i < file_index.members(); ++i )
          {
          const Block & db = file_index.dblock( i );
          const Block & mb = file_index.mblock( i );
          std::printf( "%5ld %15llu %15llu %15llu %15llu\n",
                       i + 1, db.pos(), db.size(), mb.pos(), mb.size() );
          }
        first_post = true;	// reprint heading after list of members
        }
      std::fflush( stdout );
      }
    }
  if( verbosity >= 0 && files > 1 )
    {
    if( verbosity >= 1 ) std::fputs( "                      ", stdout );
    list_line( total_uncomp, total_comp, "(totals)" );
    std::fflush( stdout );
    }
  return retval;
  }
