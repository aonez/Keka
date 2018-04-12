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

class Range_decoder
  {
  enum { buffer_size = 16384 };
  unsigned long long partial_member_pos;
  uint8_t * const buffer;	// input buffer
  int pos;			// current pos in buffer
  int stream_pos;		// when reached, a new block must be read
  uint32_t code;
  uint32_t range;
  const int infd;		// input file descriptor
  bool at_stream_end;

  bool read_block();

  Range_decoder( const Range_decoder & );	// declared as private
  void operator=( const Range_decoder & );	// declared as private

public:
  explicit Range_decoder( const int ifd )
    :
    partial_member_pos( 0 ),
    buffer( new uint8_t[buffer_size] ),
    pos( 0 ),
    stream_pos( 0 ),
    code( 0 ),
    range( 0xFFFFFFFFU ),
    infd( ifd ),
    at_stream_end( false )
    {}

  ~Range_decoder() { delete[] buffer; }

  bool finished() { return pos >= stream_pos && !read_block(); }
  unsigned long long member_position() const { return partial_member_pos + pos; }

  void reset_member_position()
    { partial_member_pos = 0; partial_member_pos -= pos; }

  uint8_t get_byte()
    {
    // 0xFF avoids decoder error if member is truncated at EOS marker
    if( finished() ) return 0xFF;
    return buffer[pos++];
    }

  int read_data( uint8_t * const outbuf, const int size )
    {
    int sz = 0;
    while( sz < size && !finished() )
      {
      const int rd = std::min( size - sz, stream_pos - pos );
      std::memcpy( outbuf + sz, buffer + pos, rd );
      pos += rd;
      sz += rd;
      }
    return sz;
    }

  void load()
    {
    code = 0;
    for( int i = 0; i < 5; ++i ) code = (code << 8) | get_byte();
    range = 0xFFFFFFFFU;
    code &= range;		// make sure that first byte is discarded
    }

  void normalize()
    {
    if( range <= 0x00FFFFFFU )
      { range <<= 8; code = (code << 8) | get_byte(); }
    }

  unsigned decode( const int num_bits )
    {
    unsigned symbol = 0;
    for( int i = num_bits; i > 0; --i )
      {
      normalize();
      range >>= 1;
//      symbol <<= 1;
//      if( code >= range ) { code -= range; symbol |= 1; }
      const bool bit = ( code >= range );
      symbol = ( symbol << 1 ) + bit;
      code -= range & ( 0U - bit );
      }
    return symbol;
    }

  unsigned decode_bit( Bit_model & bm )
    {
    normalize();
    const uint32_t bound = ( range >> bit_model_total_bits ) * bm.probability;
    if( code < bound )
      {
      range = bound;
      bm.probability += (bit_model_total - bm.probability) >> bit_model_move_bits;
      return 0;
      }
    else
      {
      range -= bound;
      code -= bound;
      bm.probability -= bm.probability >> bit_model_move_bits;
      return 1;
      }
    }

  unsigned decode_tree3( Bit_model bm[] )
    {
    unsigned symbol = 1;
    symbol = ( symbol << 1 ) | decode_bit( bm[symbol] );
    symbol = ( symbol << 1 ) | decode_bit( bm[symbol] );
    symbol = ( symbol << 1 ) | decode_bit( bm[symbol] );
    return symbol & 7;
    }

  unsigned decode_tree6( Bit_model bm[] )
    {
    unsigned symbol = 1;
    symbol = ( symbol << 1 ) | decode_bit( bm[symbol] );
    symbol = ( symbol << 1 ) | decode_bit( bm[symbol] );
    symbol = ( symbol << 1 ) | decode_bit( bm[symbol] );
    symbol = ( symbol << 1 ) | decode_bit( bm[symbol] );
    symbol = ( symbol << 1 ) | decode_bit( bm[symbol] );
    symbol = ( symbol << 1 ) | decode_bit( bm[symbol] );
    return symbol & 0x3F;
    }

  unsigned decode_tree8( Bit_model bm[] )
    {
    unsigned symbol = 1;
    for( int i = 0; i < 8; ++i )
      symbol = ( symbol << 1 ) | decode_bit( bm[symbol] );
    return symbol & 0xFF;
    }

  unsigned decode_tree_reversed( Bit_model bm[], const int num_bits )
    {
    unsigned model = 1;
    unsigned symbol = 0;
    for( int i = 0; i < num_bits; ++i )
      {
      const unsigned bit = decode_bit( bm[model] );
      model = ( model << 1 ) + bit;
      symbol |= ( bit << i );
      }
    return symbol;
    }

  unsigned decode_tree_reversed4( Bit_model bm[] )
    {
    unsigned symbol = decode_bit( bm[1] );
    unsigned model = 2 + symbol;
    unsigned bit = decode_bit( bm[model] );
    model = ( model << 1 ) + bit; symbol |= ( bit << 1 );
    bit = decode_bit( bm[model] );
    model = ( model << 1 ) + bit; symbol |= ( bit << 2 );
    symbol |= ( decode_bit( bm[model] ) << 3 );
    return symbol;
    }

  unsigned decode_matched( Bit_model bm[], unsigned match_byte )
    {
    Bit_model * const bm1 = bm + 0x100;
    unsigned symbol = 1;
    while( symbol < 0x100 )
      {
      const unsigned match_bit = ( match_byte <<= 1 ) & 0x100;
      const unsigned bit = decode_bit( bm1[match_bit+symbol] );
      symbol = ( symbol << 1 ) | bit;
      if( match_bit != bit << 8 )
        {
        while( symbol < 0x100 )
          symbol = ( symbol << 1 ) | decode_bit( bm[symbol] );
        break;
        }
      }
    return symbol & 0xFF;
    }

  unsigned decode_len( Len_model & lm, const int pos_state )
    {
    if( decode_bit( lm.choice1 ) == 0 )
      return decode_tree3( lm.bm_low[pos_state] );
    if( decode_bit( lm.choice2 ) == 0 )
      return len_low_symbols + decode_tree3( lm.bm_mid[pos_state] );
    return len_low_symbols + len_mid_symbols + decode_tree8( lm.bm_high );
    }
  };


class LZ_decoder
  {
  unsigned long long partial_data_pos;
  Range_decoder & rdec;
  const unsigned dictionary_size;
  uint8_t * const buffer;	// output buffer
  unsigned pos;			// current pos in buffer
  unsigned stream_pos;		// first byte not yet written to file
  uint32_t crc_;
  const int outfd;		// output file descriptor
  bool pos_wrapped;

  void flush_data();
  bool verify_trailer( const Pretty_print & pp ) const;

  uint8_t peek_prev() const
    {
    if( pos > 0 ) return buffer[pos-1];
    if( pos_wrapped ) return buffer[dictionary_size-1];
    return 0;			// prev_byte of first byte
    }

  uint8_t peek( const unsigned distance ) const
    {
    const unsigned i = ( ( pos > distance ) ? 0 : dictionary_size ) +
                       pos - distance - 1;
    return buffer[i];
    }

  void put_byte( const uint8_t b )
    {
    buffer[pos] = b;
    if( ++pos >= dictionary_size ) flush_data();
    }

  void copy_block( const unsigned distance, unsigned len )
    {
    unsigned lpos = pos, i = lpos - distance - 1;
    bool fast, fast2;
    if( lpos > distance )
      {
      fast = ( len < dictionary_size - lpos );
      fast2 = ( fast && len <= lpos - i );
      }
    else
      {
      i += dictionary_size;
      fast = ( len < dictionary_size - i );	// (i == pos) may happen
      fast2 = ( fast && len <= i - lpos );
      }
    if( fast )					// no wrap
      {
      pos += len;
      if( fast2 )				// no wrap, no overlap
        std::memcpy( buffer + lpos, buffer + i, len );
      else
        for( ; len > 0; --len ) buffer[lpos++] = buffer[i++];
      }
    else for( ; len > 0; --len )
      {
      buffer[pos] = buffer[i];
      if( ++pos >= dictionary_size ) flush_data();
      if( ++i >= dictionary_size ) i = 0;
      }
    }

  LZ_decoder( const LZ_decoder & );		// declared as private
  void operator=( const LZ_decoder & );		// declared as private

public:
  LZ_decoder( Range_decoder & rde, const unsigned dict_size, const int ofd )
    :
    partial_data_pos( 0 ),
    rdec( rde ),
    dictionary_size( dict_size ),
    buffer( new uint8_t[dictionary_size] ),
    pos( 0 ),
    stream_pos( 0 ),
    crc_( 0xFFFFFFFFU ),
    outfd( ofd ),
    pos_wrapped( false )
    {}

  ~LZ_decoder() { delete[] buffer; }

  unsigned crc() const { return crc_ ^ 0xFFFFFFFFU; }
  unsigned long long data_position() const { return partial_data_pos + pos; }

  int decode_member( const Pretty_print & pp );
  };
