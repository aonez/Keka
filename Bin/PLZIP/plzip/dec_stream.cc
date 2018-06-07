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


namespace {

enum { max_packet_size = 1 << 20 };
unsigned long long in_size = 0;
unsigned long long out_size = 0;


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
  unsigned icheck_counter;
  unsigned iwait_counter;
  unsigned ocheck_counter;
  unsigned owait_counter;
private:
  int receive_worker_id;	// worker queue currently receiving packets
  int deliver_worker_id;	// worker queue currently delivering packets
  Slot_tally slot_tally;		// limits the number of input packets
  std::vector< std::queue< Packet * > > ipacket_queues;
  std::vector< std::queue< Packet * > > opacket_queues;
  int num_working;			// number of workers still running
  const int num_workers;		// number of workers
  const unsigned out_slots;		// max output packets per queue
  pthread_mutex_t imutex;
  pthread_cond_t iav_or_eof;	// input packet available or splitter done
  pthread_mutex_t omutex;
  pthread_cond_t oav_or_exit;	// output packet available or all workers exited
  std::vector< pthread_cond_t > slot_av;	// output slot available
  bool eof;					// splitter done

  Packet_courier( const Packet_courier & );	// declared as private
  void operator=( const Packet_courier & );	// declared as private

public:
  Packet_courier( const int workers, const int in_slots, const int oslots )
    : icheck_counter( 0 ), iwait_counter( 0 ),
      ocheck_counter( 0 ), owait_counter( 0 ),
      receive_worker_id( 0 ), deliver_worker_id( 0 ),
      slot_tally( in_slots ), ipacket_queues( workers ),
      opacket_queues( workers ), num_working( workers ),
      num_workers( workers ), out_slots( oslots ), slot_av( workers ),
      eof( false )
    {
    xinit_mutex( &imutex ); xinit_cond( &iav_or_eof );
    xinit_mutex( &omutex ); xinit_cond( &oav_or_exit );
    for( unsigned i = 0; i < slot_av.size(); ++i ) xinit_cond( &slot_av[i] );
    }

  ~Packet_courier()
    {
    for( unsigned i = 0; i < slot_av.size(); ++i ) xdestroy_cond( &slot_av[i] );
    xdestroy_cond( &oav_or_exit ); xdestroy_mutex( &omutex );
    xdestroy_cond( &iav_or_eof ); xdestroy_mutex( &imutex );
    }

  // make a packet with data received from splitter
  // if data == 0, move to next queue
  void receive_packet( uint8_t * const data, const int size )
    {
    Packet * const ipacket = new Packet( data, size );
    if( data )
      { in_size += size; slot_tally.get_slot(); }  // wait for a free slot
    xlock( &imutex );
    ipacket_queues[receive_worker_id].push( ipacket );
    xbroadcast( &iav_or_eof );
    xunlock( &imutex );
    if( !data && ++receive_worker_id >= num_workers )
      receive_worker_id = 0;
    }

  // distribute a packet to a worker
  Packet * distribute_packet( const int worker_id )
    {
    Packet * ipacket = 0;
    xlock( &imutex );
    ++icheck_counter;
    while( ipacket_queues[worker_id].empty() && !eof )
      {
      ++iwait_counter;
      xwait( &iav_or_eof, &imutex );
      }
    if( !ipacket_queues[worker_id].empty() )
      {
      ipacket = ipacket_queues[worker_id].front();
      ipacket_queues[worker_id].pop();
      }
    xunlock( &imutex );
    if( ipacket )
      { if( ipacket->data ) slot_tally.leave_slot(); }
    else
      {
      // notify muxer when last worker exits
      xlock( &omutex );
      if( --num_working == 0 ) xsignal( &oav_or_exit );
      xunlock( &omutex );
      }
    return ipacket;
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

  void add_out_size( const unsigned long long partial_out_size )
    {
    xlock( &omutex );
    out_size += partial_out_size;
    xunlock( &omutex );
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
    if( !slot_tally.all_free() || !eof || num_working != 0 ) return false;
    for( int i = 0; i < num_workers; ++i )
      if( !ipacket_queues[i].empty() ) return false;
    for( int i = 0; i < num_workers; ++i )
      if( !opacket_queues[i].empty() ) return false;
    return true;
    }
  };


// Search forward from 'pos' for "LZIP" (Boyer-Moore algorithm)
// Returns pos of found string or 'pos+size' if not found.
//
int find_magic( const uint8_t * const buffer, const int pos, const int size )
  {
  const uint8_t table[256] = {
    4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
    4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
    4,4,4,4,4,4,4,4,4,1,4,4,3,4,4,4,4,4,4,4,4,4,4,4,4,4,2,4,4,4,4,4,
    4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
    4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
    4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
    4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
    4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4 };

  for( int i = pos; i <= pos + size - 4; i += table[buffer[i+3]] )
    if( buffer[i]   == 'L' && buffer[i+1] == 'Z' &&
        buffer[i+2] == 'I' && buffer[i+3] == 'P' )
      return i;				// magic string found
  return pos + size;
  }


struct Splitter_arg
  {
  Packet_courier * courier;
  const Pretty_print * pp;
  int infd;
  };


       // split data from input file into chunks and pass them to
       // courier for packaging and distribution to workers.
extern "C" void * dsplitter_s( void * arg )
  {
  const Splitter_arg & tmp = *(Splitter_arg *)arg;
  Packet_courier & courier = *tmp.courier;
  const Pretty_print & pp = *tmp.pp;
  const int infd = tmp.infd;
  const int hsize = File_header::size;
  const int tsize = File_trailer::size;
  const int buffer_size = max_packet_size;
  const int base_buffer_size = tsize + buffer_size + hsize;
  uint8_t * const base_buffer = new( std::nothrow ) uint8_t[base_buffer_size];
  if( !base_buffer ) { pp( "Not enough memory." ); cleanup_and_fail(); }
  uint8_t * const buffer = base_buffer + tsize;

  int size = readblock( infd, buffer, buffer_size + hsize ) - hsize;
  bool at_stream_end = ( size < buffer_size );
  if( size != buffer_size && errno )
    { pp(); show_error( "Read error", errno ); cleanup_and_fail(); }
  if( size + hsize < min_member_size )
    { pp( "Input file is too short." ); cleanup_and_fail( 2 ); }
  const File_header & header = *(File_header *)buffer;
  if( !header.verify_magic() )
    { pp( bad_magic_msg ); cleanup_and_fail( 2 ); }
  if( !header.verify_version() )
    { pp( bad_version( header.version() ) ); cleanup_and_fail( 2 ); }
  const unsigned dictionary_size = header.dictionary_size();
  if( !isvalid_ds( dictionary_size ) )
    { pp( bad_dict_msg ); cleanup_and_fail( 2 ); }
  show_header( dictionary_size );

  unsigned long long partial_member_size = 0;
  while( true )
    {
    int pos = 0;
    for( int newpos = 1; newpos <= size; ++newpos )
      {
      newpos = find_magic( buffer, newpos, size + 4 - newpos );
      if( newpos <= size )
        {
        const File_trailer & trailer = *(File_trailer *)(buffer + newpos - tsize);
        const unsigned long long member_size = trailer.member_size();
        if( partial_member_size + newpos - pos == member_size )
          {						// header found
          const File_header & header = *(File_header *)(buffer + newpos);
          if( !header.verify_version() )
            { pp( bad_version( header.version() ) ); cleanup_and_fail( 2 ); }
          const unsigned dictionary_size = header.dictionary_size();
          if( !isvalid_ds( dictionary_size ) )
            { pp( bad_dict_msg ); cleanup_and_fail( 2 ); }
          uint8_t * const data = new( std::nothrow ) uint8_t[newpos - pos];
          if( !data ) { pp( "Not enough memory." ); cleanup_and_fail(); }
          std::memcpy( data, buffer + pos, newpos - pos );
          courier.receive_packet( data, newpos - pos );
          courier.receive_packet( 0, 0 );	// end of member token
          partial_member_size = 0;
          pos = newpos;
          }
        }
      }

    if( at_stream_end )
      {
      uint8_t * data = new( std::nothrow ) uint8_t[size + hsize - pos];
      if( !data ) { pp( "Not enough memory." ); cleanup_and_fail(); }
      std::memcpy( data, buffer + pos, size + hsize - pos );
      courier.receive_packet( data, size + hsize - pos );
      courier.receive_packet( 0, 0 );		// end of member token
      break;
      }
    if( pos < buffer_size )
      {
      partial_member_size += buffer_size - pos;
      uint8_t * data = new( std::nothrow ) uint8_t[buffer_size - pos];
      if( !data ) { pp( "Not enough memory." ); cleanup_and_fail(); }
      std::memcpy( data, buffer + pos, buffer_size - pos );
      courier.receive_packet( data, buffer_size - pos );
      }
    std::memcpy( base_buffer, base_buffer + buffer_size, tsize + hsize );
    size = readblock( infd, buffer + hsize, buffer_size );
    at_stream_end = ( size < buffer_size );
    if( size != buffer_size && errno )
      { pp(); show_error( "Read error", errno ); cleanup_and_fail(); }
    }
  delete[] base_buffer;
  courier.finish();			// no more packets to send
  return 0;
  }


struct Worker_arg
  {
  Packet_courier * courier;
  const Pretty_print * pp;
  int worker_id;
  bool ignore_trailing;
  bool testing;
  };


       // consume packets from courier, decompress their contents and,
       // if not testing, give the produced packets to courier.
extern "C" void * dworker_s( void * arg )
  {
  const Worker_arg & tmp = *(Worker_arg *)arg;
  Packet_courier & courier = *tmp.courier;
  const Pretty_print & pp = *tmp.pp;
  const int worker_id = tmp.worker_id;
  const bool ignore_trailing = tmp.ignore_trailing;
  const bool testing = tmp.testing;

  uint8_t * new_data = new( std::nothrow ) uint8_t[max_packet_size];
  LZ_Decoder * const decoder = LZ_decompress_open();
  if( !new_data || !decoder || LZ_decompress_errno( decoder ) != LZ_ok )
    { pp( "Not enough memory." ); cleanup_and_fail(); }
  unsigned long long partial_out_size = 0;
  int new_pos = 0;
  bool trailing_data_found = false;

  while( true )
    {
    const Packet * const ipacket = courier.distribute_packet( worker_id );
    if( !ipacket ) break;		// no more packets to process
    if( !ipacket->data ) LZ_decompress_finish( decoder );

    int written = 0;
    while( !trailing_data_found )
      {
      if( LZ_decompress_write_size( decoder ) > 0 && written < ipacket->size )
        {
        const int wr = LZ_decompress_write( decoder, ipacket->data + written,
                                            ipacket->size - written );
        if( wr < 0 ) internal_error( "library error (LZ_decompress_write)." );
        written += wr;
        if( written > ipacket->size )
          internal_error( "ipacket size exceeded in worker." );
        }
      while( !trailing_data_found )	// read and pack decompressed data
        {
        const int rd = LZ_decompress_read( decoder, new_data + new_pos,
                                           max_packet_size - new_pos );
        if( rd < 0 )
          {
          if( LZ_decompress_errno( decoder ) == LZ_header_error )
            {
            trailing_data_found = true;
            if( !ignore_trailing )
              { pp( trailing_msg ); cleanup_and_fail( 2 ); }
            }
          else
            cleanup_and_fail( decompress_read_error( decoder, pp, worker_id ) );
          }
        else new_pos += rd;
        if( new_pos > max_packet_size )
          internal_error( "opacket size exceeded in worker." );
        if( new_pos == max_packet_size || trailing_data_found ||
            LZ_decompress_finished( decoder ) == 1 )
          {
          if( !testing && new_pos > 0 )		// make data packet
            {
            Packet * const opacket = new Packet( new_data, new_pos );
            courier.collect_packet( opacket, worker_id );
            new_data = new( std::nothrow ) uint8_t[max_packet_size];
            if( !new_data ) { pp( "Not enough memory." ); cleanup_and_fail(); }
            }
          partial_out_size += new_pos;
          new_pos = 0;
          if( trailing_data_found || LZ_decompress_finished( decoder ) == 1 )
            {
            if( !testing )			// end of member token
              courier.collect_packet( new Packet, worker_id );
            LZ_decompress_reset( decoder );	// prepare for new member
            break;
            }
          }
        if( rd == 0 ) break;
        }
      if( !ipacket->data || written == ipacket->size ) break;
      }
    if( ipacket->data ) delete[] ipacket->data;
    delete ipacket;
    }

  delete[] new_data;
  courier.add_out_size( partial_out_size );
  if( LZ_decompress_member_position( decoder ) != 0 )
    { pp( "Error, some data remains in decoder." ); cleanup_and_fail(); }
  if( LZ_decompress_close( decoder ) < 0 )
    { pp( "LZ_decompress_close failed." ); cleanup_and_fail(); }
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


    // init the courier, then start the splitter and the workers and,
    // if not testing, call the muxer.
int dec_stream( const int num_workers, const int infd, const int outfd,
                const Pretty_print & pp, const int debug_level,
                const bool ignore_trailing )
  {
  const int in_slots_per_worker = 2;
  const int out_slots = 32;
  const int in_slots = ( INT_MAX / num_workers >= in_slots_per_worker ) ?
                       num_workers * in_slots_per_worker : INT_MAX;
  in_size = 0;
  out_size = 0;
  Packet_courier courier( num_workers, in_slots, out_slots );

  Splitter_arg splitter_arg;
  splitter_arg.courier = &courier;
  splitter_arg.pp = &pp;
  splitter_arg.infd = infd;

  pthread_t splitter_thread;
  int errcode = pthread_create( &splitter_thread, 0, dsplitter_s, &splitter_arg );
  if( errcode )
    { show_error( "Can't create splitter thread", errcode ); cleanup_and_fail(); }

  Worker_arg * worker_args = new( std::nothrow ) Worker_arg[num_workers];
  pthread_t * worker_threads = new( std::nothrow ) pthread_t[num_workers];
  if( !worker_args || !worker_threads )
    { pp( "Not enough memory." ); cleanup_and_fail(); }
  for( int i = 0; i < num_workers; ++i )
    {
    worker_args[i].courier = &courier;
    worker_args[i].pp = &pp;
    worker_args[i].worker_id = i;
    worker_args[i].ignore_trailing = ignore_trailing;
    worker_args[i].testing = ( outfd < 0 );
    errcode = pthread_create( &worker_threads[i], 0, dworker_s, &worker_args[i] );
    if( errcode )
      { show_error( "Can't create worker threads", errcode ); cleanup_and_fail(); }
    }

  if( outfd >= 0 ) muxer( courier, pp, outfd );

  for( int i = num_workers - 1; i >= 0; --i )
    {
    errcode = pthread_join( worker_threads[i], 0 );
    if( errcode )
      { show_error( "Can't join worker threads", errcode ); cleanup_and_fail(); }
    }
  delete[] worker_threads;
  delete[] worker_args;

  errcode = pthread_join( splitter_thread, 0 );
  if( errcode )
    { show_error( "Can't join splitter thread", errcode ); cleanup_and_fail(); }

  if( verbosity >= 2 && out_size > 0 && in_size > 0 )
    std::fprintf( stderr, "%6.3f:1, %6.3f bits/byte, %5.2f%% saved.  ",
                  (double)out_size / in_size,
                  ( 8.0 * in_size ) / out_size,
                  100.0 * ( 1.0 - ( (double)in_size / out_size ) ) );
  if( verbosity >= 4 )
    std::fprintf( stderr, "decompressed %9llu, compressed %9llu.  ",
                  out_size, in_size );

  if( verbosity >= 1 ) std::fputs( (outfd < 0) ? "ok\n" : "done\n", stderr );

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
