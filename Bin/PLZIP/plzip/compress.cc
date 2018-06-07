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
#include <queue>
#include <string>
#include <vector>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>
#include "lzlib.h" //#include <lzlib.h>

#include "lzip.h"

#ifndef LLONG_MAX
#define LLONG_MAX  0x7FFFFFFFFFFFFFFFLL
#endif


// Returns the number of bytes really read.
// If (returned value < size) and (errno == 0), means EOF was reached.
//
int readblock( const int fd, uint8_t * const buf, const int size )
  {
  int sz = 0;
  errno = 0;
  while( sz < size )
    {
    const int n = read( fd, buf + sz, size - sz );
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
int writeblock( const int fd, const uint8_t * const buf, const int size )
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


void xinit_mutex( pthread_mutex_t * const mutex )
  {
  const int errcode = pthread_mutex_init( mutex, 0 );
  if( errcode )
    { show_error( "pthread_mutex_init", errcode ); cleanup_and_fail(); }
  }

void xinit_cond( pthread_cond_t * const cond )
  {
  const int errcode = pthread_cond_init( cond, 0 );
  if( errcode )
    { show_error( "pthread_cond_init", errcode ); cleanup_and_fail(); }
  }


void xdestroy_mutex( pthread_mutex_t * const mutex )
  {
  const int errcode = pthread_mutex_destroy( mutex );
  if( errcode )
    { show_error( "pthread_mutex_destroy", errcode ); cleanup_and_fail(); }
  }

void xdestroy_cond( pthread_cond_t * const cond )
  {
  const int errcode = pthread_cond_destroy( cond );
  if( errcode )
    { show_error( "pthread_cond_destroy", errcode ); cleanup_and_fail(); }
  }


void xlock( pthread_mutex_t * const mutex )
  {
  const int errcode = pthread_mutex_lock( mutex );
  if( errcode )
    { show_error( "pthread_mutex_lock", errcode ); cleanup_and_fail(); }
  }


void xunlock( pthread_mutex_t * const mutex )
  {
  const int errcode = pthread_mutex_unlock( mutex );
  if( errcode )
    { show_error( "pthread_mutex_unlock", errcode ); cleanup_and_fail(); }
  }


void xwait( pthread_cond_t * const cond, pthread_mutex_t * const mutex )
  {
  const int errcode = pthread_cond_wait( cond, mutex );
  if( errcode )
    { show_error( "pthread_cond_wait", errcode ); cleanup_and_fail(); }
  }


void xsignal( pthread_cond_t * const cond )
  {
  const int errcode = pthread_cond_signal( cond );
  if( errcode )
    { show_error( "pthread_cond_signal", errcode ); cleanup_and_fail(); }
  }


void xbroadcast( pthread_cond_t * const cond )
  {
  const int errcode = pthread_cond_broadcast( cond );
  if( errcode )
    { show_error( "pthread_cond_broadcast", errcode ); cleanup_and_fail(); }
  }


namespace {

unsigned long long in_size = 0;
unsigned long long out_size = 0;
const char * const mem_msg = "Not enough memory. Try a smaller dictionary size";


struct Packet			// data block with a serial number
  {
  uint8_t * data;
  int size;			// number of bytes in data (if any)
  unsigned id;			// serial number assigned as received
  Packet( uint8_t * const d, const int s, const unsigned i )
    : data( d ), size( s ), id( i ) {}
  };


class Packet_courier			// moves packets around
  {
public:
  unsigned icheck_counter;
  unsigned iwait_counter;
  unsigned ocheck_counter;
  unsigned owait_counter;
private:
  unsigned receive_id;			// id assigned to next packet received
  unsigned deliver_id;			// id of next packet to be delivered
  Slot_tally slot_tally;		// limits the number of input packets
  std::queue< Packet * > packet_queue;
  std::vector< const Packet * > circular_buffer;
  int num_working;			// number of workers still running
  const int num_slots;			// max packets in circulation
  pthread_mutex_t imutex;
  pthread_cond_t iav_or_eof;	// input packet available or splitter done
  pthread_mutex_t omutex;
  pthread_cond_t oav_or_exit;	// output packet available or all workers exited
  bool eof;				// splitter done

  Packet_courier( const Packet_courier & );	// declared as private
  void operator=( const Packet_courier & );	// declared as private

public:
  Packet_courier( const int workers, const int slots )
    : icheck_counter( 0 ), iwait_counter( 0 ),
      ocheck_counter( 0 ), owait_counter( 0 ),
      receive_id( 0 ), deliver_id( 0 ),
      slot_tally( slots ), circular_buffer( slots, (Packet *) 0 ),
      num_working( workers ), num_slots( slots ), eof( false )
    {
    xinit_mutex( &imutex ); xinit_cond( &iav_or_eof );
    xinit_mutex( &omutex ); xinit_cond( &oav_or_exit );
    }

  ~Packet_courier()
    {
    xdestroy_cond( &oav_or_exit ); xdestroy_mutex( &omutex );
    xdestroy_cond( &iav_or_eof ); xdestroy_mutex( &imutex );
    }

  // make a packet with data received from splitter
  void receive_packet( uint8_t * const data, const int size )
    {
    Packet * const ipacket = new Packet( data, size, receive_id++ );
    slot_tally.get_slot();		// wait for a free slot
    xlock( &imutex );
    packet_queue.push( ipacket );
    xsignal( &iav_or_eof );
    xunlock( &imutex );
    }

  // distribute a packet to a worker
  Packet * distribute_packet()
    {
    Packet * ipacket = 0;
    xlock( &imutex );
    ++icheck_counter;
    while( packet_queue.empty() && !eof )
      {
      ++iwait_counter;
      xwait( &iav_or_eof, &imutex );
      }
    if( !packet_queue.empty() )
      {
      ipacket = packet_queue.front();
      packet_queue.pop();
      }
    xunlock( &imutex );
    if( !ipacket )
      {
      // notify muxer when last worker exits
      xlock( &omutex );
      if( --num_working == 0 ) xsignal( &oav_or_exit );
      xunlock( &omutex );
      }
    return ipacket;
    }

  // collect a packet from a worker
  void collect_packet( const Packet * const opacket )
    {
    const int i = opacket->id % num_slots;
    xlock( &omutex );
    // id collision shouldn't happen
    if( circular_buffer[i] != 0 )
      internal_error( "id collision in collect_packet." );
    // merge packet into circular buffer
    circular_buffer[i] = opacket;
    if( opacket->id == deliver_id ) xsignal( &oav_or_exit );
    xunlock( &omutex );
    }

  // deliver packets to muxer
  void deliver_packets( std::vector< const Packet * > & packet_vector )
    {
    xlock( &omutex );
    ++ocheck_counter;
    int i = deliver_id % num_slots;
    while( circular_buffer[i] == 0 && num_working > 0 )
      {
      ++owait_counter;
      xwait( &oav_or_exit, &omutex );
      }
    packet_vector.clear();
    while( true )
      {
      const Packet * const opacket = circular_buffer[i];
      if( !opacket ) break;
      packet_vector.push_back( opacket );
      circular_buffer[i] = 0;
      ++deliver_id;
      i = deliver_id % num_slots;
      }
    xunlock( &omutex );
    if( packet_vector.size() )		// return slots to the tally
      slot_tally.leave_slots( packet_vector.size() );
    }

  void finish()			// splitter has no more packets to send
    {
    xlock( &imutex );
    eof = true;
    xbroadcast( &iav_or_eof );
    xunlock( &imutex );
    }

  bool finished()		// all packets delivered to muxer
    {
    if( !slot_tally.all_free() || !eof || !packet_queue.empty() ||
        num_working != 0 ) return false;
    for( int i = 0; i < num_slots; ++i )
      if( circular_buffer[i] != 0 ) return false;
    return true;
    }
  };


struct Splitter_arg
  {
  Packet_courier * courier;
  const Pretty_print * pp;
  int infd;
  int data_size;
  int offset;
  };


       // split data from input file into chunks and pass them to
       // courier for packaging and distribution to workers.
extern "C" void * csplitter( void * arg )
  {
  const Splitter_arg & tmp = *(Splitter_arg *)arg;
  Packet_courier & courier = *tmp.courier;
  const Pretty_print & pp = *tmp.pp;
  const int infd = tmp.infd;
  const int data_size = tmp.data_size;
  const int offset = tmp.offset;

  for( bool first_post = true; ; first_post = false )
    {
    uint8_t * const data = new( std::nothrow ) uint8_t[offset+data_size];
    if( !data ) { pp( mem_msg ); cleanup_and_fail(); }
    const int size = readblock( infd, data + offset, data_size );
    if( size != data_size && errno )
      { pp(); show_error( "Read error", errno ); cleanup_and_fail(); }

    if( size > 0 || first_post )	// first packet may be empty
      {
      in_size += size;
      courier.receive_packet( data, size );
      if( size < data_size ) break;	// EOF
      }
    else
      {
      delete[] data;
      break;
      }
    }
  courier.finish();			// no more packets to send
  return 0;
  }


struct Worker_arg
  {
  Packet_courier * courier;
  const Pretty_print * pp;
  int dictionary_size;
  int match_len_limit;
  int offset;
  };


       // get packets from courier, replace their contents, and return
       // them to courier.
extern "C" void * cworker( void * arg )
  {
  const Worker_arg & tmp = *(Worker_arg *)arg;
  Packet_courier & courier = *tmp.courier;
  const Pretty_print & pp = *tmp.pp;
  const int dictionary_size = tmp.dictionary_size;
  const int match_len_limit = tmp.match_len_limit;
  const int offset = tmp.offset;

  while( true )
    {
    Packet * const packet = courier.distribute_packet();
    if( !packet ) break;		// no more packets to process

    const bool fast = dictionary_size == 65535 && match_len_limit == 16;
    const int dict_size = fast ? dictionary_size :
                          std::max( std::min( dictionary_size, packet->size ),
                                    LZ_min_dictionary_size() );
    LZ_Encoder * const encoder =
      LZ_compress_open( dict_size, match_len_limit, LLONG_MAX );
    if( !encoder || LZ_compress_errno( encoder ) != LZ_ok )
      {
      if( !encoder || LZ_compress_errno( encoder ) == LZ_mem_error )
        pp( mem_msg );
      else
        internal_error( "invalid argument to encoder." );
      cleanup_and_fail();
      }

    int written = 0;
    int new_pos = 0;
    while( true )
      {
      if( LZ_compress_write_size( encoder ) > 0 )
        {
        if( written < packet->size )
          {
          const int wr = LZ_compress_write( encoder,
                                            packet->data + offset + written,
                                            packet->size - written );
          if( wr < 0 ) internal_error( "library error (LZ_compress_write)." );
          written += wr;
          }
        if( written >= packet->size ) LZ_compress_finish( encoder );
        }
      const int rd = LZ_compress_read( encoder, packet->data + new_pos,
                                       offset + written - new_pos );
      if( rd < 0 )
        {
        pp();
        if( verbosity >= 0 )
          std::fprintf( stderr, "LZ_compress_read error: %s\n",
                        LZ_strerror( LZ_compress_errno( encoder ) ) );
        cleanup_and_fail();
        }
      new_pos += rd;
      if( new_pos >= offset + written )
        internal_error( "packet size exceeded in worker." );
      if( LZ_compress_finished( encoder ) == 1 ) break;
      }

    if( LZ_compress_close( encoder ) < 0 )
      { pp( "LZ_compress_close failed." ); cleanup_and_fail(); }

    if( packet->size > 0 ) show_progress( packet->size );
    packet->size = new_pos;
    courier.collect_packet( packet );
    }
  return 0;
  }


     // get from courier the processed and sorted packets, and write
     // their contents to the output file.
void muxer( Packet_courier & courier, const Pretty_print & pp, const int outfd )
  {
  std::vector< const Packet * > packet_vector;
  while( true )
    {
    courier.deliver_packets( packet_vector );
    if( packet_vector.empty() ) break;		// all workers exited

    for( unsigned i = 0; i < packet_vector.size(); ++i )
      {
      const Packet * const opacket = packet_vector[i];
      out_size += opacket->size;

      const int wr = writeblock( outfd, opacket->data, opacket->size );
      if( wr != opacket->size )
        { pp(); show_error( "Write error", errno ); cleanup_and_fail(); }
      delete[] opacket->data;
      delete opacket;
      }
    }
  }

} // end namespace


    // init the courier, then start the splitter and the workers and
    // call the muxer.
int compress( const int data_size, const int dictionary_size,
              const int match_len_limit, const int num_workers,
              const int infd, const int outfd,
              const Pretty_print & pp, const int debug_level )
  {
  const int offset = data_size / 8;
  const int slots_per_worker = 2;
  const int num_slots =
    ( ( num_workers > 1 ) ? num_workers * slots_per_worker : 1 );
  in_size = 0;
  out_size = 0;
  Packet_courier courier( num_workers, num_slots );

  Splitter_arg splitter_arg;
  splitter_arg.courier = &courier;
  splitter_arg.pp = &pp;
  splitter_arg.infd = infd;
  splitter_arg.data_size = data_size;
  splitter_arg.offset = offset;

  pthread_t splitter_thread;
  int errcode = pthread_create( &splitter_thread, 0, csplitter, &splitter_arg );
  if( errcode )
    { show_error( "Can't create splitter thread", errcode ); cleanup_and_fail(); }

  Worker_arg worker_arg;
  worker_arg.courier = &courier;
  worker_arg.pp = &pp;
  worker_arg.dictionary_size = dictionary_size;
  worker_arg.match_len_limit = match_len_limit;
  worker_arg.offset = offset;

  pthread_t * worker_threads = new( std::nothrow ) pthread_t[num_workers];
  if( !worker_threads ) { pp( mem_msg ); cleanup_and_fail(); }
  for( int i = 0; i < num_workers; ++i )
    {
    errcode = pthread_create( worker_threads + i, 0, cworker, &worker_arg );
    if( errcode )
      { show_error( "Can't create worker threads", errcode ); cleanup_and_fail(); }
    }

  muxer( courier, pp, outfd );

  for( int i = num_workers - 1; i >= 0; --i )
    {
    errcode = pthread_join( worker_threads[i], 0 );
    if( errcode )
      { show_error( "Can't join worker threads", errcode ); cleanup_and_fail(); }
    }
  delete[] worker_threads;

  errcode = pthread_join( splitter_thread, 0 );
  if( errcode )
    { show_error( "Can't join splitter thread", errcode ); cleanup_and_fail(); }

  if( verbosity >= 1 )
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

  if( debug_level & 1 )
    std::fprintf( stderr,
      "any worker tried to consume from splitter %8u times\n"
      "any worker had to wait                    %8u times\n"
      "muxer tried to consume from workers       %8u times\n"
      "muxer had to wait                         %8u times\n",
      courier.icheck_counter,
      courier.iwait_counter,
      courier.ocheck_counter,
      courier.owait_counter );

  if( !courier.finished() ) internal_error( "courier not finished." );
  return 0;
  }
