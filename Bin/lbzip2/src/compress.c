/*-
  compress.c -- high-level compression routines

  Copyright (C) 2011, 2012, 2014 Mikolaj Izdebski
  Copyright (C) 2008, 2009, 2010 Laszlo Ersek

  This file is part of lbzip2.

  lbzip2 is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  lbzip2 is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with lbzip2.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "common.h"

#include "main.h"               /* bs100k */
#include "encode.h"             /* encode() */
#include "process.h"            /* struct process */

/* transmit threshold */
#define TRANSM_THRESH 2


struct in_blk {
  struct position pos;

  void *buffer;
  size_t size;

  const char unsigned *next;
  size_t left;
};


struct work_blk {
  struct position pos;
  struct position next;

  struct encoder_state *enc;
  void *buffer;
  size_t size;
  uint32_t crc;
  size_t weight;
};


static struct pqueue(struct in_blk *) coll_q;
static struct pqueue(struct work_blk *) trans_q;
static struct pqueue(struct work_blk *) reord_q;
static struct position order;
static uintmax_t next_id;       /* next free input block sequence number */
static uint32_t combined_crc;
static bool collect_token = true;
static struct work_blk *unfinished_work;


static bool
can_collect(void)
{
  return !ultra && !empty(coll_q) && work_units > 0;
}


static void
do_collect(void)
{
  struct in_blk *iblk;
  struct work_blk *wblk;

  iblk = dequeue(coll_q);
  --work_units;
  sched_unlock();

  wblk = XMALLOC(struct work_blk);

  wblk->pos = iblk->pos;
  wblk->next = iblk->pos;

  /* Allocate an encoder with given block size and default parameters. */
  wblk->enc = encoder_init(bs100k * 100000u, CLUSTER_FACTOR);

  /* Collect as much data as we can. */
  wblk->weight = iblk->left;
  collect(wblk->enc, iblk->next, &iblk->left);
  wblk->weight -= iblk->left;
  iblk->next += wblk->weight;

  if (0u < iblk->left) {
    ++wblk->next.minor;
    ++iblk->pos.minor;
    sched_lock();
    enqueue(coll_q, iblk);
    sched_unlock();
  }
  else {
    ++wblk->next.major;
    wblk->next.minor = 0;
    source_release_buffer(iblk->buffer);
    free(iblk);
  }

  /* Do the hard work. */
  wblk->size = encode(wblk->enc, &wblk->crc);

  sched_lock();
  enqueue(trans_q, wblk);
}


static bool
can_collect_seq(void)
{
  return ultra && collect_token && (!empty(coll_q) || (eof && unfinished_work != NULL)) && (work_units > 0 || unfinished_work != NULL);
}


static void
do_collect_seq(void)
{
  struct in_blk *iblk;
  struct work_blk *wblk;
  bool done = true;

  wblk = unfinished_work;
  unfinished_work = NULL;
  if (wblk == NULL)
    --work_units;

  iblk = NULL;
  if (!empty(coll_q))
    iblk = dequeue(coll_q);

  collect_token = false;
  sched_unlock();

  /* Allocate an encoder with given block size and default parameters. */
  if (wblk == NULL) {
    wblk = XMALLOC(struct work_blk);
    wblk->pos = iblk->pos;
    wblk->next = iblk->pos;
    wblk->enc = encoder_init(bs100k * 100000u, CLUSTER_FACTOR);
    wblk->weight = 0;
  }

  /* Collect as much data as we can. */
  if (iblk != NULL) {
    wblk->size = iblk->left;
    done = collect(wblk->enc, iblk->next, &iblk->left);
    wblk->size -= iblk->left;
    wblk->weight += wblk->size;
    iblk->next += wblk->size;

    if (0u < iblk->left) {
      ++wblk->next.minor;
      ++iblk->pos.minor;
      sched_lock();
      enqueue(coll_q, iblk);
      sched_unlock();
    }
    else {
      ++wblk->next.major;
      wblk->next.minor = 0;
      source_release_buffer(iblk->buffer);
      free(iblk);
    }
  }

  if (!done) {
    sched_lock();
    collect_token = true;
    unfinished_work = wblk;
    return;
  }

  sched_lock();
  collect_token = true;
  sched_unlock();

  /* Do the hard work. */
  wblk->size = encode(wblk->enc, &wblk->crc);

  sched_lock();
  enqueue(trans_q, wblk);
}


static bool
can_transmit(void)
{
  return !empty(trans_q) && (out_slots > TRANSM_THRESH ||
                             (out_slots > 0 && pos_eq(peek(trans_q)->pos, order)));
}


static void
do_transmit(void)
{
  struct work_blk *wblk;

  wblk = dequeue(trans_q);
  --out_slots;
  sched_unlock();

  /* Allocate the output buffer and transmit the block into it. */
  wblk->buffer = XNMALLOC((wblk->size + 3) / 4, uint32_t);

  transmit(wblk->enc, wblk->buffer);

  sched_lock();
  ++work_units;
  enqueue(reord_q, wblk);
}


static bool
can_reorder(void)
{
  return !empty(reord_q) && pos_eq(peek(reord_q)->pos, order);
}


static void
do_reorder(void)
{
  struct work_blk *wblk;

  wblk = dequeue(reord_q);
  order = wblk->next;

  sink_write_buffer(wblk->buffer, wblk->size, wblk->weight);
  combined_crc = combine_crc(combined_crc, wblk->crc);

  free(wblk);
}


static bool
can_terminate(void)
{
  return eof && empty(coll_q) &&
      work_units == num_worker && out_slots == total_out_slots;
}


static void
on_input_avail(void *buffer, size_t size)
{
  struct in_blk *iblk = XMALLOC(struct in_blk);

  iblk->pos.major = next_id++;
  iblk->pos.minor = 0u;
  iblk->buffer = buffer;
  iblk->size = size;
  iblk->next = buffer;
  iblk->left = size;

  sched_lock();
  enqueue(coll_q, iblk);
  sched_unlock();
}


static void
on_write_complete(void *buffer)
{
  free(buffer);

  sched_lock();
  ++out_slots;
  sched_unlock();
}


static void
write_header(void)
{
  uint8_t buffer[HEADER_SIZE];

  buffer[0] = 0x42;
  buffer[1] = 0x5A;
  buffer[2] = 0x68;
  buffer[3] = 0x30 + bs100k;

  xwrite(buffer, HEADER_SIZE);
}


static void
write_trailer(void)
{
  uint8_t buffer[TRAILER_SIZE];

  buffer[0] = 0x17;
  buffer[1] = 0x72;
  buffer[2] = 0x45;
  buffer[3] = 0x38;
  buffer[4] = 0x50;
  buffer[5] = 0x90;
  buffer[6] = combined_crc >> 24;
  buffer[7] = (combined_crc >> 16) & 0xFF;
  buffer[8] = (combined_crc >> 8) & 0xFF;
  buffer[9] = combined_crc & 0xFF;

  xwrite(buffer, TRAILER_SIZE);
}


static void
init(void)
{
  pqueue_init(coll_q, in_slots);
  pqueue_init(trans_q, work_units);
  pqueue_init(reord_q, out_slots);

  next_id = 0;
  order.major = 0;
  order.minor = 0;

  assert(1 <= bs100k && bs100k <= 9);
  combined_crc = 0;

  write_header();
}


static void
uninit(void)
{
  write_trailer();

  pqueue_uninit(coll_q);
  pqueue_uninit(trans_q);
  pqueue_uninit(reord_q);
}


static const struct task task_list[] = {
  { "collect_seq", can_collect_seq, do_collect_seq },
  { "reorder",     can_reorder,     do_reorder     },
  { "transmit",    can_transmit,    do_transmit    },
  { "collect",     can_collect,     do_collect     },
  { NULL,          NULL,            NULL           },
};

const struct process compression = {
  task_list,
  init,
  uninit,
  can_terminate,
  on_input_avail,
  on_write_complete,
};
