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
#include "file_index.h"


namespace {

enum { max_packet_size = 1 << 20 };


struct Packet			// data block
  {
  uint8_t * data;		// data == 0 means end of member
  int size;			// number of bytes in data (if any)
  explicit Packet( uint8_t * const d = 0, const int s = 0 )
    : data( d ), size( s ) {}
  };


class Packet_courier			// moves packets around
  {
public:
  unsigned ocheck_counter;
  unsigned owait_counter;
private:
  int deliver_worker_id;	// worker queue currently delivering packets
  std::vector< std::queue< Packet * > > opacket_queues;
  int num_working;			// number of workers still running
  const int num_workers;		// number of workers
  const unsigned out_slots;		// max output packets per queue
  pthread_mutex_t omutex;
  pthread_cond_t oav_or_exit;	// output packet available or all workers exited
  std::vector< pthread_cond_t > slot_av;	// output slot available

  Packet_courier( const Packet_courier & );	// declared as private
  void operator=( const Packet_courier & );	// declared as private

public:
  Packet_courier( const int workers, const int slots )
    : ocheck_counter( 0 ), owait_counter( 0 ),
      deliver_worker_id( 0 ),
      opacket_queues( workers ), num_working( workers ),
      num_workers( workers ), out_slots( slots ), slot_av( workers )
    {
    xinit_mutex( &omutex ); xinit_cond( &oav_or_exit );
    for( unsigned i = 0; i < slot_av.size(); ++i ) xinit_cond( &slot_av[i] );
    }

  ~Packet_courier()
    {
    for( unsigned i = 0; i < slot_av.size(); ++i ) xdestroy_cond( &slot_av[i] );
    xdestroy_cond( &oav_or_exit ); xdestroy_mutex( &omutex );
    }

  void worker_finished()
    {
    // notify muxer when last worker exits
    xlock( &omutex );
    if( --num_working == 0 ) xsignal( &oav_or_exit );
    xunlock( &omutex );
    }

  // collect a packet from a worker
  void collect_packet( Packet * const opacket, const int worker_id )
    {
    xlock( &omutex );
    if( opacket->data )
      {
      while( opacket_queues[worker_id].size() >= out_slots )
        xwait( &slot_av[worker_id], &omutex );
      }
    opacket_queues[worker_id].push( opacket );
    if( worker_id == deliver_worker_id ) xsignal( &oav_or_exit );
    xunlock( &omutex );
    }

  // deliver a packet to muxer
  // if packet data == 0, move to next queue and wait again
  Packet * deliver_packet()
    {
    Packet * opacket = 0;
    xlock( &omutex );
    ++ocheck_counter;
    while( true )
      {
      while( opacket_queues[deliver_worker_id].empty() && num_working > 0 )
        {
        ++owait_counter;
        xwait( &oav_or_exit, &omutex );
        }
      if( opacket_queues[deliver_worker_id].empty() ) break;
      opacket = opacket_queues[deliver_worker_id].front();
      opacket_queues[deliver_worker_id].pop();
      if( opacket_queues[deliver_worker_id].size() + 1 == out_slots )
        xsignal( &slot_av[deliver_worker_id] );
      if( opacket->data ) break;
      if( ++deliver_worker_id >= num_workers ) deliver_worker_id = 0;
      delete opacket; opacket = 0;
      }
    xunlock( &omutex );
    return opacket;
    }

  bool finished()		// all packets delivered to muxer
    {
    if( num_working != 0 ) return false;
    for( int i = 0; i < num_workers; ++i )
      if( !opacket_queues[i].empty() ) return false;
    return true;
    }
  };


struct Worker_arg
  {
  const File_index * file_index;
  Packet_courier * courier;
  const Pretty_print * pp;
  int worker_id;
  int num_workers;
  int infd;
  };


       // read members from file, decompress their contents, and
       // give the produced packets to courier.
extern "C" void * dworker_o( void * arg )
  {
  const Worker_arg & tmp = *(Worker_arg *)arg;
  const File_index & file_index = *tmp.file_index;
  Packet_courier & courier = *tmp.courier;
  const Pretty_print & pp = *tmp.pp;
  const int worker_id = tmp.worker_id;
  const int num_workers = tmp.num_workers;
  const int infd = tmp.infd;
  const int buffer_size = 65536;

  uint8_t * new_data = new( std::nothrow ) uint8_t[max_packet_size];
  uint8_t * const ibuffer = new( std::nothrow ) uint8_t[buffer_size];
  LZ_Decoder * const decoder = LZ_decompress_open();
  if( !new_data || !ibuffer || !decoder ||
      LZ_decompress_errno( decoder ) != LZ_ok )
    { pp( "Not enough memory." ); cleanup_and_fail(); }
  int new_pos = 0;

  for( long i = worker_id; i < file_index.members(); i += num_workers )
    {
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
      while( true )			// read and pack decompressed data
        {
        const int rd = LZ_decompress_read( decoder, new_data + new_pos,
                                           max_packet_size - new_pos );
        if( rd < 0 )
          cleanup_and_fail( decompress_read_error( decoder, pp, worker_id ) );
        new_pos += rd;
        if( new_pos > max_packet_size )
          internal_error( "opacket size exceeded in worker." );
        if( new_pos == max_packet_size ||
            LZ_decompress_finished( decoder ) == 1 )
          {
          if( new_pos > 0 )			// make data packet
            {
            Packet * const opacket = new Packet( new_data, new_pos );
            courier.collect_packet( opacket, worker_id );
            new_pos = 0;
            new_data = new( std::nothrow ) uint8_t[max_packet_size];
            if( !new_data ) { pp( "Not enough memory." ); cleanup_and_fail(); }
            }
          if( LZ_decompress_finished( decoder ) == 1 )
            {					// end of member token
            courier.collect_packet( new Packet, worker_id );
            LZ_decompress_reset( decoder );	// prepare for new member
            break;
            }
          }
        if( rd == 0 ) break;
        }
      }
    }

  delete[] ibuffer; delete[] new_data;
  if( LZ_decompress_member_position( decoder ) != 0 )
    { pp( "Error, some data remains in decoder." ); cleanup_and_fail(); }
  if( LZ_decompress_close( decoder ) < 0 )
    { pp( "LZ_decompress_close failed." ); cleanup_and_fail(); }
  courier.worker_finished();
  return 0;
  }


     // get from courier the processed and sorted packets, and write
     // their contents to the output file.
void muxer( Packet_courier & courier, const Pretty_print & pp, const int outfd )
  {
  while( true )
    {
    Packet * const opacket = courier.deliver_packet();
    if( !opacket ) break;	// queue is empty. all workers exited

    const int wr = writeblock( outfd, opacket->data, opacket->size );
    if( wr != opacket->size )
      { pp(); show_error( "Write error", errno ); cleanup_and_fail(); }
    delete[] opacket->data;
    delete opacket;
    }
  }

} // end namespace


    // init the courier, then start the workers and call the muxer.
int dec_stdout( const int num_workers, const int infd, const int outfd,
                const Pretty_print & pp, const int debug_level,
                const File_index & file_index )
  {
  const int out_slots = 32;
  Packet_courier courier( num_workers, out_slots );

  Worker_arg * worker_args = new( std::nothrow ) Worker_arg[num_workers];
  pthread_t * worker_threads = new( std::nothrow ) pthread_t[num_workers];
  if( !worker_args || !worker_threads )
    { pp( "Not enough memory." ); cleanup_and_fail(); }
  for( int i = 0; i < num_workers; ++i )
    {
    worker_args[i].file_index = &file_index;
    worker_args[i].courier = &courier;
    worker_args[i].pp = &pp;
    worker_args[i].worker_id = i;
    worker_args[i].num_workers = num_workers;
    worker_args[i].infd = infd;
    const int errcode =
      pthread_create( &worker_threads[i], 0, dworker_o, &worker_args[i] );
    if( errcode )
      { show_error( "Can't create worker threads", errcode ); cleanup_and_fail(); }
    }

  muxer( courier, pp, outfd );

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

  if( verbosity >= 1 ) std::fputs( "done\n", stderr );

  if( debug_level & 1 )
    std::fprintf( stderr,
      "muxer tried to consume from workers       %8u times\n"
      "muxer had to wait                         %8u times\n",
      courier.ocheck_counter,
      courier.owait_counter );

  if( !courier.finished() ) internal_error( "courier not finished." );
  return 0;
  }
