/*  Buffer to buffer example - Test program for the lzlib library
    Copyright (C) 2010-2017 Antonio Diaz Diaz.

    This program is free software: you have unlimited permission
    to copy, distribute and modify it.

    Usage is:
      bbexample filename

    This program is an example of how buffer-to-buffer
    compression/decompression can be implemented using lzlib.
*/

#include <errno.h>
#include <limits.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lzlib.h"


/* Returns the address of a malloc'd buffer containing the file data and
   its size in '*size'.
   In case of error, returns 0 and does not modify '*size'.
*/
uint8_t * read_file( const char * const name, long * const size )
  {
  long buffer_size = 1 << 20, file_size;
  uint8_t * buffer, * tmp;
  FILE * const f = fopen( name, "rb" );
  if( !f )
    {
    fprintf( stderr, "bbexample: Can't open input file '%s': %s\n",
             name, strerror( errno ) );
    return 0;
    }

  buffer = (uint8_t *)malloc( buffer_size );
  if( !buffer )
    { fputs( "bbexample: Not enough memory.\n", stderr ); return 0; }
  file_size = fread( buffer, 1, buffer_size, f );
  while( file_size >= buffer_size )
    {
    if( buffer_size >= LONG_MAX )
      {
      fprintf( stderr, "bbexample: Input file '%s' is too large.\n", name );
      free( buffer ); return 0;
      }
    buffer_size = ( buffer_size <= LONG_MAX / 2 ) ? 2 * buffer_size : LONG_MAX;
    tmp = (uint8_t *)realloc( buffer, buffer_size );
    if( !tmp )
      { fputs( "bbexample: Not enough memory.\n", stderr );
        free( buffer ); return 0; }
    buffer = tmp;
    file_size += fread( buffer + file_size, 1, buffer_size - file_size, f );
    }
  if( ferror( f ) || !feof( f ) )
    {
    fprintf( stderr, "bbexample: Error reading file '%s': %s\n",
             name, strerror( errno ) );
    free( buffer ); return 0;
    }
  fclose( f );
  *size = file_size;
  return buffer;
  }


/* Compresses 'size' bytes from 'data'. Returns the address of a
   malloc'd buffer containing the compressed data and its size in
   '*out_sizep'.
   In case of error, returns 0 and does not modify '*out_sizep'.
*/
uint8_t * bbcompress( const uint8_t * const data, const long size,
                      const int level, long * const out_sizep )
  {
  struct Lzma_options
    {
    int dictionary_size;		/* 4 KiB .. 512 MiB */
    int match_len_limit;		/* 5 .. 273 */
    };
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
  struct Lzma_options encoder_options;
  const unsigned long long member_size = 0x7FFFFFFFFFFFFFFFULL;	/* INT64_MAX */
  struct LZ_Encoder * encoder;
  uint8_t * new_data;
  const long delta_size = ( size / 4 ) + 64;	/* size may be zero */
  long new_data_size = delta_size;		/* initial size */
  long new_pos = 0;
  long written = 0;
  bool error = false;

  if( level < 0 || level > 9 ) return 0;
  encoder_options = option_mapping[level];

  if( encoder_options.dictionary_size > size && level != 0 )
    encoder_options.dictionary_size = size;		/* saves memory */
  if( encoder_options.dictionary_size < LZ_min_dictionary_size() )
    encoder_options.dictionary_size = LZ_min_dictionary_size();
  encoder = LZ_compress_open( encoder_options.dictionary_size,
                              encoder_options.match_len_limit, member_size );
  if( !encoder || LZ_compress_errno( encoder ) != LZ_ok )
    { LZ_compress_close( encoder ); return 0; }

  new_data = (uint8_t *)malloc( new_data_size );
  if( !new_data )
    { LZ_compress_close( encoder ); return 0; }

  while( true )
    {
    int rd;
    if( LZ_compress_write_size( encoder ) > 0 )
      {
      if( written < size )
        {
        const int wr = LZ_compress_write( encoder, data + written,
                                          size - written );
        if( wr < 0 ) { error = true; break; }
        written += wr;
        }
      if( written >= size ) LZ_compress_finish( encoder );
      }
    rd = LZ_compress_read( encoder, new_data + new_pos,
                           new_data_size - new_pos );
    if( rd < 0 ) { error = true; break; }
    new_pos += rd;
    if( LZ_compress_finished( encoder ) == 1 ) break;
    if( new_pos >= new_data_size )
      {
      uint8_t * tmp;
      if( new_data_size > LONG_MAX - delta_size ) { error = true; break; }
      new_data_size += delta_size;
      tmp = (uint8_t *)realloc( new_data, new_data_size );
      if( !tmp ) { error = true; break; }
      new_data = tmp;
      }
    }

  if( LZ_compress_close( encoder ) < 0 ) error = true;
  if( error ) { free( new_data ); return 0; }
  *out_sizep = new_pos;
  return new_data;
  }


/* Decompresses 'size' bytes from 'data'. Returns the address of a
   malloc'd buffer containing the decompressed data and its size in
   '*out_sizep'.
   In case of error, returns 0 and does not modify '*out_sizep'.
*/
uint8_t * bbdecompress( const uint8_t * const data, const long size,
                        long * const out_sizep )
  {
  struct LZ_Decoder * const decoder = LZ_decompress_open();
  uint8_t * new_data;
  const long delta_size = size;			/* size must be > zero */
  long new_data_size = delta_size;		/* initial size */
  long new_pos = 0;
  long written = 0;
  bool error = false;
  if( !decoder || LZ_decompress_errno( decoder ) != LZ_ok )
    { LZ_decompress_close( decoder ); return 0; }

  new_data = (uint8_t *)malloc( new_data_size );
  if( !new_data )
    { LZ_decompress_close( decoder ); return 0; }

  while( true )
    {
    int rd;
    if( LZ_decompress_write_size( decoder ) > 0 )
      {
      if( written < size )
        {
        const int wr = LZ_decompress_write( decoder, data + written,
                                            size - written );
        if( wr < 0 ) { error = true; break; }
        written += wr;
        }
      if( written >= size ) LZ_decompress_finish( decoder );
      }
    rd = LZ_decompress_read( decoder, new_data + new_pos,
                             new_data_size - new_pos );
    if( rd < 0 ) { error = true; break; }
    new_pos += rd;
    if( LZ_decompress_finished( decoder ) == 1 ) break;
    if( new_pos >= new_data_size )
      {
      uint8_t * tmp;
      if( new_data_size > LONG_MAX - delta_size ) { error = true; break; }
      new_data_size += delta_size;
      tmp = (uint8_t *)realloc( new_data, new_data_size );
      if( !tmp ) { error = true; break; }
      new_data = tmp;
      }
    }

  if( LZ_decompress_close( decoder ) < 0 ) error = true;
  if( error ) { free( new_data ); return 0; }
  *out_sizep = new_pos;
  return new_data;
  }


int main( const int argc, const char * const argv[] )
  {
  uint8_t * in_buffer;
  long in_size = 0;
  int level;

  if( argc < 2 )
    {
    fputs( "Usage: bbexample filename\n", stderr );
    return 1;
    }

  in_buffer = read_file( argv[1], &in_size );
  if( !in_buffer ) return 1;

  for( level = 0; level <= 9; ++level )
    {
    uint8_t * mid_buffer, * out_buffer;
    long mid_size = 0, out_size = 0;

    mid_buffer = bbcompress( in_buffer, in_size, level, &mid_size );
    if( !mid_buffer )
      {
      fputs( "bbexample: Not enough memory or compress error.\n", stderr );
      return 1;
      }

    out_buffer = bbdecompress( mid_buffer, mid_size, &out_size );
    if( !out_buffer )
      {
      fputs( "bbexample: Not enough memory or decompress error.\n", stderr );
      return 1;
      }

    if( in_size != out_size ||
        ( in_size > 0 && memcmp( in_buffer, out_buffer, in_size ) != 0 ) )
      {
      fputs( "bbexample: Decompressed data differs from original.\n", stderr );
      return 1;
      }

    free( out_buffer );
    free( mid_buffer );
    }
  free( in_buffer );
  return 0;
  }
