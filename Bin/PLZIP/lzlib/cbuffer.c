/*  Lzlib - Compression library for the lzip format
    Copyright (C) 2009-2017 Antonio Diaz Diaz.

    This library is free software. Redistribution and use in source and
    binary forms, with or without modification, are permitted provided
    that the following conditions are met:

    1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/

struct Circular_buffer
  {
  uint8_t * buffer;
  unsigned buffer_size;		/* capacity == buffer_size - 1 */
  unsigned get;			/* buffer is empty when get == put */
  unsigned put;
  };

static inline void Cb_reset( struct Circular_buffer * const cb )
  { cb->get = 0; cb->put = 0; }

static inline bool Cb_init( struct Circular_buffer * const cb,
                            const unsigned buf_size )
  {
  cb->buffer_size = buf_size + 1;
  cb->get = 0;
  cb->put = 0;
  cb->buffer =
    ( cb->buffer_size > 1 ) ? (uint8_t *)malloc( cb->buffer_size ) : 0;
  return ( cb->buffer != 0 );
  }

static inline void Cb_free( struct Circular_buffer * const cb )
  { free( cb->buffer ); cb->buffer = 0; }

static inline unsigned Cb_used_bytes( const struct Circular_buffer * const cb )
  { return ( (cb->get <= cb->put) ? 0 : cb->buffer_size ) + cb->put - cb->get; }

static inline unsigned Cb_free_bytes( const struct Circular_buffer * const cb )
  { return ( (cb->get <= cb->put) ? cb->buffer_size : 0 ) - cb->put + cb->get - 1; }

static inline uint8_t Cb_get_byte( struct Circular_buffer * const cb )
  {
  const uint8_t b = cb->buffer[cb->get];
  if( ++cb->get >= cb->buffer_size ) cb->get = 0;
  return b;
  }

static inline void Cb_put_byte( struct Circular_buffer * const cb,
                                const uint8_t b )
  {
  cb->buffer[cb->put] = b;
  if( ++cb->put >= cb->buffer_size ) cb->put = 0;
  }


static bool Cb_unread_data( struct Circular_buffer * const cb,
                            const unsigned size )
  {
  if( size > Cb_free_bytes( cb ) ) return false;
  if( cb->get >= size ) cb->get -= size;
  else cb->get = cb->buffer_size - size + cb->get;
  return true;
  }


/* Copies up to 'out_size' bytes to 'out_buffer' and updates 'get'.
   Returns the number of bytes copied.
*/
static unsigned Cb_read_data( struct Circular_buffer * const cb,
                              uint8_t * const out_buffer,
                              const unsigned out_size )
  {
  unsigned size = 0;
  if( out_size == 0 ) return 0;
  if( cb->get > cb->put )
    {
    size = min( cb->buffer_size - cb->get, out_size );
    if( size > 0 )
      {
      memcpy( out_buffer, cb->buffer + cb->get, size );
      cb->get += size;
      if( cb->get >= cb->buffer_size ) cb->get = 0;
      }
    }
  if( cb->get < cb->put )
    {
    const unsigned size2 = min( cb->put - cb->get, out_size - size );
    if( size2 > 0 )
      {
      memcpy( out_buffer + size, cb->buffer + cb->get, size2 );
      cb->get += size2;
      size += size2;
      }
    }
  return size;
  }


/* Copies up to 'in_size' bytes from 'in_buffer' and updates 'put'.
   Returns the number of bytes copied.
*/
static unsigned Cb_write_data( struct Circular_buffer * const cb,
                               const uint8_t * const in_buffer,
                               const unsigned in_size )
  {
  unsigned size = 0;
  if( in_size == 0 ) return 0;
  if( cb->put >= cb->get )
    {
    size = min( cb->buffer_size - cb->put - (cb->get == 0), in_size );
    if( size > 0 )
      {
      memcpy( cb->buffer + cb->put, in_buffer, size );
      cb->put += size;
      if( cb->put >= cb->buffer_size ) cb->put = 0;
      }
    }
  if( cb->put < cb->get )
    {
    const unsigned size2 = min( cb->get - cb->put - 1, in_size - size );
    if( size2 > 0 )
      {
      memcpy( cb->buffer + cb->put, in_buffer + size, size2 );
      cb->put += size2;
      size += size2;
      }
    }
  return size;
  }
