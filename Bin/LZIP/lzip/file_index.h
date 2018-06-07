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

#ifndef INT64_MAX
#define INT64_MAX  0x7FFFFFFFFFFFFFFFLL
#endif


class Block
  {
  long long pos_, size_;		// pos + size <= INT64_MAX

public:
  Block( const long long p, const long long s ) : pos_( p ), size_( s ) {}

  long long pos() const { return pos_; }
  long long size() const { return size_; }
  long long end() const { return pos_ + size_; }

  void pos( const long long p ) { pos_ = p; }
  void size( const long long s ) { size_ = s; }
  };


class File_index
  {
  struct Member
    {
    Block dblock, mblock;		// data block, member block
    unsigned dictionary_size;

    Member( const long long dp, const long long ds,
            const long long mp, const long long ms, const unsigned dict_size )
      : dblock( dp, ds ), mblock( mp, ms ), dictionary_size( dict_size ) {}
    };

  std::vector< Member > member_vector;
  std::string error_;
  const long long isize;
  int retval_;

  void set_errno_error( const char * const msg );
  void set_num_error( const char * const msg, unsigned long long num );
  bool skip_trailing_data( const int fd, long long & pos );

public:
  File_index( const int infd, const bool ignore_trailing );

  long members() const { return member_vector.size(); }
  const std::string & error() const { return error_; }
  int retval() const { return retval_; }

  long long udata_size() const
    { if( member_vector.empty() ) return 0;
      return member_vector.back().dblock.end(); }

  long long cdata_size() const
    { if( member_vector.empty() ) return 0;
      return member_vector.back().mblock.end(); }

  // total size including trailing data (if any)
  long long file_size() const
    { if( isize >= 0 ) return isize; else return 0; }

  const Block & dblock( const long i ) const
    { return member_vector[i].dblock; }
  const Block & mblock( const long i ) const
    { return member_vector[i].mblock; }
  unsigned dictionary_size( const long i ) const
    { return member_vector[i].dictionary_size; }
  };
