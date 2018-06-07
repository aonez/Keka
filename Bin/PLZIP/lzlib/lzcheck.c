/*  Lzcheck - Test program for the lzlib library
    Copyright (C) 2009-2017 Antonio Diaz Diaz.

    This program is free software: you have unlimited permission
    to copy, distribute and modify it.

    Usage is:
      lzcheck filename.txt

    This program reads the specified text file and then compresses it,
    line by line, to test the flushing mechanism and the member
    restart/reset/sync functions.
*/

#define _FILE_OFFSET_BITS 64

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lzlib.h"

#ifndef min
  #define min(x,y) ((x) <= (y) ? (x) : (y))
#endif


enum { buffer_size = 32768 };
uint8_t in_buffer[buffer_size];
uint8_t mid_buffer[buffer_size];
uint8_t out_buffer[buffer_size];


int lzcheck( FILE * const file, const int dictionary_size )
  {
  const int match_len_limit = 16;
  const unsigned long long member_size = 0x7FFFFFFFFFFFFFFFULL;	/* INT64_MAX */
  struct LZ_Encoder * encoder;
  struct LZ_Decoder * decoder;
  int retval = 0;

  encoder = LZ_compress_open( dictionary_size, match_len_limit, member_size );
  if( !encoder || LZ_compress_errno( encoder ) != LZ_ok )
    {
    const bool mem_error = ( LZ_compress_errno( encoder ) == LZ_mem_error );
    LZ_compress_close( encoder );
    if( mem_error )
      {
      fputs( "lzcheck: Not enough memory.\n", stderr );
      return 1;
      }
    fputs( "lzcheck: internal error: Invalid argument to encoder.\n", stderr );
    return 3;
    }

  decoder = LZ_decompress_open();
  if( !decoder || LZ_decompress_errno( decoder ) != LZ_ok )
    {
    LZ_decompress_close( decoder );
    fputs( "lzcheck: Not enough memory.\n", stderr );
    return 1;
    }

  while( retval <= 1 )
    {
    int i, l, r;
    const int read_size = fread( in_buffer, 1, buffer_size, file );
    if( read_size <= 0 ) break;			/* end of file */

    for( l = 0, r = 1; r <= read_size; l = r, ++r )
      {
      int in_size, mid_size, out_size;
      while( r < read_size && in_buffer[r-1] != '\n' ) ++r;
      in_size = LZ_compress_write( encoder, in_buffer + l, r - l );
      if( in_size < r - l ) r = l + in_size;
      LZ_compress_sync_flush( encoder );
      mid_size = LZ_compress_read( encoder, mid_buffer, buffer_size );
      if( mid_size < 0 )
        {
        fprintf( stderr, "lzcheck: LZ_compress_read error: %s\n",
                 LZ_strerror( LZ_compress_errno( encoder ) ) );
        retval = 3; break;
        }
      LZ_decompress_write( decoder, mid_buffer, mid_size );
      out_size = LZ_decompress_read( decoder, out_buffer, buffer_size );
      if( out_size < 0 )
        {
        fprintf( stderr, "lzcheck: LZ_decompress_read error: %s\n",
                 LZ_strerror( LZ_decompress_errno( decoder ) ) );
        retval = 3; break;
        }

      if( out_size != in_size || memcmp( in_buffer + l, out_buffer, out_size ) )
        {
        fprintf( stderr, "lzcheck: Sync error at pos %d  in_size = %d, out_size = %d\n",
                 l, in_size, out_size );
        for( i = 0; i < in_size; ++i )
          fputc( in_buffer[l+i], stderr );
        if( in_buffer[l+in_size-1] != '\n' )
          fputc( '\n', stderr );
        for( i = 0; i < out_size; ++i )
          fputc( out_buffer[i], stderr );
        fputc( '\n', stderr );
        retval = 1;
        }
      }
    }

  if( retval <= 1 )
    {
    rewind( file );
    if( LZ_compress_finish( encoder ) < 0 ||
        LZ_decompress_write( decoder, mid_buffer, LZ_compress_read( encoder, mid_buffer, buffer_size ) ) < 0 ||
        LZ_decompress_read( decoder, out_buffer, buffer_size ) != 0 ||
        LZ_compress_restart_member( encoder, member_size ) < 0 )
      {
      fprintf( stderr, "lzcheck: Can't finish member: %s\n",
               LZ_strerror( LZ_decompress_errno( decoder ) ) );
      retval = 3;
      }
    }

  while( retval <= 1 )
    {
    int i, l, r, size;
    const int read_size = fread( in_buffer, 1, buffer_size / 2, file );
    if( read_size <= 0 ) break;			/* end of file */

    for( l = 0, r = 1; r <= read_size; l = r, ++r )
      {
      int leading_garbage, in_size, mid_size, out_size;
      while( r < read_size && in_buffer[r-1] != '\n' ) ++r;
      leading_garbage = (l == 0) ? min( r, read_size / 2 ) : 0;
      in_size = LZ_compress_write( encoder, in_buffer + l, r - l );
      if( in_size < r - l ) r = l + in_size;
      LZ_compress_sync_flush( encoder );
      if( leading_garbage )
        memset( mid_buffer, in_buffer[0], leading_garbage );
      mid_size = LZ_compress_read( encoder, mid_buffer + leading_garbage,
                                   buffer_size - leading_garbage );
      if( mid_size < 0 )
        {
        fprintf( stderr, "lzcheck: LZ_compress_read error: %s\n",
                 LZ_strerror( LZ_compress_errno( encoder ) ) );
        retval = 3; break;
        }
      LZ_decompress_write( decoder, mid_buffer, mid_size + leading_garbage );
      out_size = LZ_decompress_read( decoder, out_buffer, buffer_size );
      if( out_size < 0 )
        {
        if( LZ_decompress_errno( decoder ) == LZ_header_error ||
            LZ_decompress_errno( decoder ) == LZ_data_error )
          {
          LZ_decompress_sync_to_member( decoder );  /* remove leading garbage */
          out_size = LZ_decompress_read( decoder, out_buffer, buffer_size );
          }
        if( out_size < 0 )
          {
          fprintf( stderr, "lzcheck: LZ_decompress_read error: %s\n",
                   LZ_strerror( LZ_decompress_errno( decoder ) ) );
          retval = 3; break;
          }
        }

      if( out_size != in_size || memcmp( in_buffer + l, out_buffer, out_size ) )
        {
        fprintf( stderr, "lzcheck: Sync error at pos %d  in_size = %d, out_size = %d, leading garbage = %d\n",
                 l, in_size, out_size, leading_garbage );
        for( i = 0; i < in_size; ++i )
          fputc( in_buffer[l+i], stderr );
        if( in_buffer[l+in_size-1] != '\n' )
          fputc( '\n', stderr );
        for( i = 0; i < out_size; ++i )
          fputc( out_buffer[i], stderr );
        fputc( '\n', stderr );
        retval = 1;
        }
      }
    if( retval >= 3 ) break;

    if( LZ_compress_finish( encoder ) < 0 ||
        LZ_decompress_write( decoder, mid_buffer, LZ_compress_read( encoder, mid_buffer, buffer_size ) ) < 0 ||
        LZ_decompress_read( decoder, out_buffer, buffer_size ) != 0 ||
        LZ_decompress_reset( decoder ) < 0 ||
        LZ_compress_restart_member( encoder, member_size ) < 0 )
      {
      fprintf( stderr, "lzcheck: Can't restart member: %s\n",
               LZ_strerror( LZ_decompress_errno( decoder ) ) );
      retval = 3; break;
      }

    size = min( 100, read_size );
    if( LZ_compress_write( encoder, in_buffer, size ) != size ||
        LZ_compress_finish( encoder ) < 0 ||
        LZ_decompress_write( decoder, mid_buffer, LZ_compress_read( encoder, mid_buffer, buffer_size ) ) < 0 ||
        LZ_decompress_read( decoder, out_buffer, 0 ) != 0 ||
        LZ_decompress_sync_to_member( decoder ) < 0 ||
        LZ_compress_restart_member( encoder, member_size ) < 0 )
      {
      fprintf( stderr, "lzcheck: Can't seek to next member: %s\n",
               LZ_strerror( LZ_decompress_errno( decoder ) ) );
      retval = 3; break;
      }
    }

  LZ_decompress_close( decoder );
  LZ_compress_close( encoder );
  return retval;
  }


int main( const int argc, const char * const argv[] )
  {
  FILE * file;
  int retval;

  if( argc < 2 )
    {
    fputs( "Usage: lzcheck filename.txt\n", stderr );
    return 1;
    }

  file = fopen( argv[1], "rb" );
  if( !file )
    {
    fprintf( stderr, "lzcheck: Can't open file '%s' for reading.\n", argv[1] );
    return 1;
    }
/*  fprintf( stderr, "lzcheck: Testing file '%s'\n", argv[1] ); */

  retval = lzcheck( file, 65535 );	/* 65535,16 chooses fast encoder */
  if( retval == 0 )
    { rewind( file ); retval = lzcheck( file, 1 << 20 ); }
  fclose( file );
  return retval;
  }
