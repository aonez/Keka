/*-
  expand.c -- high-level decompression routines

  Copyright (C) 2011, 2012 Mikolaj Izdebski

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

#include "decode.h"             /* decode() */
#include "main.h"               /* bs100k */
#include "process.h"            /* struct process */

#include <strings.h>            /* bzero() */


/*
  MEMORY ALLOCATION

  A single 26-byte bzip2 stream can decompress to as much as 47 MB, meaning
  compression ratio of about 1.8 million to one. (See ch255.bz2 in ../tests.)
  This case can appear in practice (for example when compressing a disk image
  with long runs of zeroes), but also someone could craft such bzip2 file
  in hope of crashing victim's system causing denial of service.
  For this reason proper resource allocation seems to be important.

  One of the simplest algorithms of allocating memory is FCFS (First-Come,
  First-Served), in which the memory is allocated to the first thread that asks
  for it. Unfortunatelly this algorithm is deadlock-prone. The only situaltion
  in which dceadlock can occur is when the block that is the next to be
  outputed is stalled waiting for memory.

  To solve this problem it's enough to make sure that this block can always
  allocate enogh resources to proceed. In lbzip2 memory is allocated to all
  threads as needed, according to FCFS policy.  However, to avoid deadlock, the
  last remaining unit of memory can be allocated only to the block which is the
  next expected on the output.
*/


/* XXX these macros are buggy, they don't even compute correct numbers... */
#ifdef ENABLE_TRACING
#define nbsx2(p) ((unsigned)((p).major*in_granul*8+((p).minor>>27)))
#define nbs2(p) nbsx2(*((const struct position *)(p)))
#define nbsd2(p) (empty(p) ? 0u : nbs2(dq_get(p,0)))
#define nbsp2(p) (empty(p) ? 0u : nbs2(peek(p)))
#endif

#define SCAN_THRESH 1u
#define EMIT_THRESH 2u
#define UNORD_THRESH (SCAN_THRESH + EMIT_THRESH)


static const char *
err2str(int err)
{
  static const char *table[] = {
    "bad stream header magic",
    "bad block header magic",
    "empty source alphabet",
    "bad number of trees",
    "no coding groups",
    "invalid selector",
    "invalid delta code",
    "invalid prefix code",
    "incomplete prefix code",
    "empty block",
    "unterminated block",
    "missing run length",
    "block CRC mismatch",
    "stream CRC mismatch",
    "block overflow",
    "primary index too large",
    "unexpected end of file",
  };

  assert(err >= ERR_MAGIC && err <= ERR_EOF);

  return table[err - ERR_MAGIC];
}


struct in_blk {
  void *buffer;
  size_t size;
  unsigned ref_count;
  uintmax_t offset;
};

struct detached_bitstream {
  struct position pos;
  uintmax_t offset;
  unsigned live;
  uint64_t buff;
  bool eof;
};

struct unord_blk {
  struct position base;
  struct detached_bitstream end_pos;
  bool complete;
  bool legitimate;
};

struct retr_blk {
  struct detached_bitstream curr_pos;
  struct position base;
  struct decoder_state ds;
  struct unord_blk *unord_link;
};

struct emit_blk {
  struct position base;
  struct decoder_state ds;
  int status;
  uintmax_t end_offset;
};

struct head_blk {
  struct position base;
  struct header hdr;
};

struct out_blk {
  struct position base;
  size_t size;
  uint32_t crc;
  uint32_t blk_sz;
  int status;
  uintmax_t end_offset;
};


static unsigned eof_missing;

static struct deque(struct in_blk *) input_q;
static uintmax_t head_offs;
static uintmax_t tail_offs;

static struct pqueue(struct retr_blk *) retr_q;
static struct pqueue(struct emit_blk *) emit_q;
static struct pqueue(struct out_blk *) reord_q;
static struct deque(struct head_blk) order_q;
static struct pqueue(struct unord_blk *) unord_q;
static bool parse_token;
static bool parsing_done;
static struct pqueue(struct detached_bitstream *) scan_q;
static uintmax_t reord_offs;

static struct detached_bitstream parser_bs;
static struct parser_state par;


#if 1
#define check_invariants()
#else
static void
check_invariants(void)
{
  unsigned i;

  /* Obvious stuff. */
  assert(head_offs <= tail_offs);
  assert(work_units <= num_worker);
  assert(out_slots <= total_out_slots);

  for (i = 0; i < size(retr_q); i++) {
    assert(retr_q.root[i]->curr_pos.offset >= head_offs);
    assert(retr_q.root[i]->curr_pos.offset >= head_offs);
  }

  for (i = 0; i < size(scan_q); i++) {
    assert(scan_q.root[i]->pos.major >= parser_bs.pos.major);
  }
}
#endif


#if 0
static void
dump_info(void)
{
  Trace(("STATUS:"
         "=============================="
         "\n\t" "eof:           %2d"
         "\n\t" "parsing_done:  %2d"
         "\n\t" "parse_token:   %2d"
         "\n\t" "work_units:    %2u/%2u"
         "\n\t" "out_slots:     %2u/%2u"
         "\n\t" "input_head:    {%6u}"
         "\n\t" "input_tail:    {%6u}"
         "\n\t" "size(input_q): %2u"
         "\n\t" "size(scan_q):  %2u {%6u}"
         "\n\t" "size(retr_q):  %2u {%6u}"
         "\n\t" "size(emit_q):  %2u {%6u}"
         "\n\t" "size(reord_q): %2u {%6u}"
         "\n\t" "size(order_q): %2u {%6u}"
         "\n\t" "size(unord_q): %2u {%6u}",
         eof, parsing_done,
         parse_token,
         work_units, num_worker,
         out_slots, total_out_slots,
         (unsigned)head_offs * 32u, (unsigned)tail_offs * 32u,
         size(input_q), size(scan_q),
         nbsp2(scan_q), size(retr_q),
         nbsp2(retr_q), size(emit_q),
         nbsp2(emit_q), size(reord_q),
         nbsp2(reord_q), size(order_q),
         nbsd2(order_q), size(unord_q),
         nbsp2(unord_q)));
}
#endif


static struct detached_bitstream
bits_init(uintmax_t offset)
{
  struct detached_bitstream bs;

  bs.live = 0u;
  bs.buff = 0u;
  bs.eof = false;
  bs.offset = offset;
  bs.pos.major = offset / (in_granul / 4);
  bs.pos.minor = (offset % (in_granul / 4)) << 32;

  return bs;
}


static bool
can_attach(struct detached_bitstream bs)
{
  /* No one should try to attach a block that has already been released. */
  assert(bs.offset >= head_offs);

  return bs.offset < tail_offs || (eof && bs.offset == tail_offs);
}

static struct bitstream
attach(struct detached_bitstream dbs)
{
  struct in_blk *blk;
  size_t id;
  uintmax_t limit;
  const uint32_t *base;
  struct bitstream bs;

  assert(dbs.offset >= head_offs);
  assert(dbs.offset <= tail_offs);

  bs.live = dbs.live;
  bs.buff = dbs.buff;
  bs.eof = dbs.eof;

  if (dbs.offset == tail_offs) {
    assert(eof);

    bs.block = NULL;
    bs.data = NULL;
    bs.limit = NULL;

    assert(!bs.eof || bs.live < 32);
    bs.eof = (bs.live < 32u);
  }
  else {
    /* TODO: binary search */
    id = 0u;
    limit = head_offs;
    do {
      blk = dq_get(input_q, id);
      limit += blk->size;
      id++;
    }
    while (dbs.offset >= limit);

    blk->ref_count++;
    bs.block = blk;

    base = blk->buffer;
    assert(dbs.offset >= blk->offset);
    assert(dbs.offset - blk->offset < blk->size);
    bs.data = base + (dbs.offset - blk->offset);
    bs.limit = base + blk->size;
  }

  check_invariants();
  sched_unlock();
  return bs;
}

static struct detached_bitstream
detach(struct bitstream bs)
{
  struct detached_bitstream dbs;
  struct in_blk *blk;
  unsigned bit_offset;
  uintmax_t offset;
  ptrdiff_t remains;

  sched_lock();

  remains = bs.limit - bs.data;
  assert(remains >= 0);

  blk = bs.block;
  offset = blk != NULL ? blk->offset + blk->size - remains : tail_offs;
  offset = min(offset, tail_offs);

  dbs.live = bs.live;
  dbs.buff = bs.buff;
  dbs.eof = bs.eof;
  dbs.offset = offset;

  bit_offset = -bs.live % 32u;
  offset -= (bs.live + 31u) / 32u;
  assert(bit_offset < 32u);
  assert(32u * dbs.offset - dbs.live == 32u * offset + bit_offset);

  dbs.pos.major = offset / (in_granul / 4);
  dbs.pos.minor = (((offset % (in_granul / 4)) << 32) + (bit_offset << 27));

  if (blk && --blk->ref_count == 0) {
    source_release_buffer(blk->buffer);
    free(blk);
  }

  return dbs;
}


/* Release any input blocks that are behind current base position. */
static void
advance(struct detached_bitstream bs)
{
  parser_bs = bs;

  /* Release input blocks. */
  while (!empty(input_q)) {
    struct in_blk *blk = dq_get(input_q, 0u);

    if (blk->offset + blk->size > bs.offset)
      break;

    head_offs += blk->size;
    (void)shift(input_q);
    if (--blk->ref_count == 0) {
      source_release_buffer(blk->buffer);
      free(blk);
    }
  }

  /* Release retrieve jobs. */
  while (!empty(retr_q) && peek(retr_q)->curr_pos.offset < head_offs) {
    struct retr_blk *rb = dequeue(retr_q);

    Trace(("Advanced over miss-recognized bit pattern at {%u}",
           nbsx2(rb->base)));

    decoder_free(&rb->ds);
    free(rb);
    work_units++;
  }

  /* Release scan jobs. */
  while (!empty(scan_q) && peek(scan_q)->offset < head_offs) {
    free(dequeue(scan_q));
  }
}


static bool
can_parse(void)
{
  return (!parsing_done && parse_token && work_units > 0u &&
          can_attach(parser_bs));
}

static void
do_parse(void)
{
  int rv;
  struct head_blk head_blk;
  struct bitstream true_bitstream;
  unsigned garbage;

  Trace(("Parser running at {%lu}",
         32ul + 32ul * parser_bs.offset - parser_bs.live));

  parse_token = 0;
  --work_units;
  true_bitstream = attach(parser_bs);
  rv = parse(&par, &head_blk.hdr, &true_bitstream, &garbage);
  advance(detach(true_bitstream));
  check_invariants();

  Trace(("Parser advancved to {%lu}",
         32ul + 32ul * parser_bs.offset - parser_bs.live));

  if (rv == MORE) {
    parse_token = true;
    work_units++;
    check_invariants();
    return;
  }

  if (rv == FINISH) {
    source_close();
    parse_token = true;
    parsing_done = true;

    assert(garbage <= 32);
    assert(parser_bs.live < 32);
    assert(parser_bs.offset <= tail_offs);
    parser_bs.live += garbage;
    if (parser_bs.live >= 32) {
      parser_bs.live -= 32;
      parser_bs.offset--;
    }
    if (parser_bs.offset == tail_offs && parser_bs.live < 8 * eof_missing) {
      failf(&ispec, "compressed data error: %s", err2str(ERR_EOF));
    }

    Trace(("Parser encountered End-Of-File at {%lu}",
           32ul + 32ul * parser_bs.offset - parser_bs.live));

    /* Release input blocks. */
    while (!empty(input_q)) {
      struct in_blk *blk = shift(input_q);

      head_offs += blk->size;
      if (--blk->ref_count == 0) {
        source_release_buffer(blk->buffer);
        free(blk);
      }
    }

    /* Release retrieve jobs. */
    while (!empty(retr_q)) {
      struct retr_blk *rb = dequeue(retr_q);

      Trace(("Parser discovered a bit pattern beyond EOF at {%u}",
             nbsx2(rb->base)));

      decoder_free(&rb->ds);
      free(rb);
      work_units++;
    }

    /* Release scan jobs. */
    while (!empty(scan_q)) {
      free(dequeue(scan_q));
    }

    /* Release unord blocks. */
    while (!empty(unord_q)) {
      struct unord_blk *ublk = dequeue(unord_q);

      if (ublk->complete)
        free(ublk);
      else {
        ublk->complete = true;
        ublk->legitimate = false;
      }
    }

    /* Release work unit. */
    work_units++;

    check_invariants();
    return;
  }

  if (rv != OK) {
    Trace(("Parser found a parse error at {%lu}",
           32ul + 32ul * parser_bs.offset - parser_bs.live));
    failf(&ispec, "compressed data error: %s", err2str(rv));
  }

  head_blk.base = parser_bs.pos;
  push(order_q, head_blk);

  while (!empty(unord_q) && pos_lt(peek(unord_q)->base, parser_bs.pos)) {
    struct unord_blk *ublk = dequeue(unord_q);

    Trace(("Parser discovered a mis-recognized bit pattern at {%u}",
           nbsx2(ublk->base)));
    if (ublk->complete) {
      free(ublk);
    }
    else {
      ublk->complete = true;
      ublk->legitimate = false;
    }
  }

  if (!empty(unord_q) && pos_eq(peek(unord_q)->base, parser_bs.pos)) {
    struct unord_blk *ublk = dequeue(unord_q);

    Trace(("Parser took advantage of pattern found by scanner at {%u}",
           nbsx2(ublk->base)));
    advance(ublk->end_pos);

    if (ublk->complete) {
      parse_token = true;
      free(ublk);
    }
    else {
      ublk->complete = true;
      ublk->legitimate = true;
    }

    /* We didn't create retrieve job after all, so release work unit
       reserved for it. */
    work_units++;
  }
  else {
    struct retr_blk *rb = XMALLOC(struct retr_blk);

    rb->unord_link = NULL;
    decoder_init(&rb->ds);
    rb->curr_pos = parser_bs;
    rb->base = parser_bs.pos;
    enqueue(retr_q, rb);
    Trace(("Parser found a unique block at {%u}", nbsx2(rb->base)));
  }

  check_invariants();
}


static bool
can_retrieve(void)
{
  return !empty(retr_q) && can_attach(peek(retr_q)->curr_pos);
}

static void
do_retrieve(void)
{
  struct retr_blk *rb;
  struct emit_blk *eb;
  struct bitstream true_bitstream;
  int rv;

  assert(!parsing_done);
  rb = dequeue(retr_q);

  true_bitstream = attach(rb->curr_pos);
  rv = retrieve(&rb->ds, &true_bitstream);
  rb->curr_pos = detach(true_bitstream);

  if (parsing_done) {
    decoder_free(&rb->ds);
    free(rb);
    work_units++;
    check_invariants();
    return;
  }

  if (rb->unord_link != NULL && rb->unord_link->complete &&
      !rb->unord_link->legitimate) {
    /* We are not yet finished retrieving, but were proven not to be
       legitimate. Continuing would be pointless, so release resources and
       abort this retrieve job. */
    Trace(("Retriever found himself redundand"));
    work_units++;
    decoder_free(&rb->ds);
    free(rb);
    check_invariants();
    return;
  }

  if (rb->unord_link == NULL || rb->unord_link->complete) {
    advance(rb->curr_pos);
    Trace(("Retriever advanced parser to {%lu}",
           32ul + 32ul * parser_bs.offset - parser_bs.live));
  }
  else {
    rb->unord_link->end_pos = rb->curr_pos;
  }

  if (rv == MORE) {
    Trace(("Retriever blocked waiting for input"));
    enqueue(retr_q, rb);
    check_invariants();
    return;
  }

  if (rb->unord_link != NULL && !rb->unord_link->complete) {
    /* Parser doesn't know about us yet, so we must be ahead of master. */
    rb->unord_link->complete = true;
    rb->unord_link->end_pos = rb->curr_pos;
  }
  else {
    /* Parser already knows about us. We must be legitimate because
       non-legititimate blocks would have already been aborted. */
    assert(rb->unord_link == NULL || (rb->unord_link->complete &&
                                      rb->unord_link->legitimate));
    /* Parser cannot be running now because we are the master so far. */
    assert(!parse_token);
    /* We are done with the bitstream, pass mastership to the parser to
       let him continue. */
    parse_token = 1;
    /* Parser knows about us, unord block is no longer needed. */
    free(rb->unord_link);
  }
  check_invariants();
  sched_unlock();

  if (rv == OK)
    decode(&rb->ds);

  eb = XMALLOC(struct emit_blk);

  eb->ds = rb->ds;
  eb->base = rb->base;
  eb->end_offset = rb->curr_pos.offset;
  free(rb);

  eb->status = rv;

  sched_lock();
  enqueue(emit_q, eb);
  check_invariants();
}


static bool
can_emit(void)
{
  return (!empty(emit_q) &&
          (out_slots > EMIT_THRESH
           || (out_slots > 0 && !empty(order_q)
               && pos_eq(peek(emit_q)->base, dq_get(order_q, 0).base))));
}

static void
do_emit(void)
{
  struct emit_blk *eb;
  struct out_blk *oblk;
  int rv;

  out_slots--;
  eb = dequeue(emit_q);
  check_invariants();
  sched_unlock();

  oblk = xmalloc(sizeof(struct out_blk) + out_granul);
  oblk->size = out_granul;
  oblk->blk_sz = eb->ds.block_size;
  rv = eb->status;
  if (rv == OK)
    rv = emit(&eb->ds, oblk + 1, &oblk->size);
  oblk->size = out_granul - oblk->size;
  oblk->status = rv;
  oblk->base = eb->base;

  if (rv == MORE) {
    oblk->end_offset = 0;
    eb->base.minor++;
    sched_lock();
    enqueue(emit_q, eb);
  }
  else {
    oblk->end_offset = eb->end_offset;
    oblk->crc = eb->ds.crc;
    decoder_free(&eb->ds);
    free(eb);
    sched_lock();
    work_units++;
  }

  enqueue(reord_q, oblk);
  check_invariants();
}


static bool
can_reorder(void)
{
  return (!empty(reord_q) &&
          ((!empty(order_q) && pos_le(peek(reord_q)->base,
                                      dq_get(order_q, 0).base))
           || (empty(order_q) && parsing_done)));
}

static void
do_reorder(void)
{
  struct out_blk *oblk;
  struct head_blk ord;
  uintmax_t offs_incr;

  if (empty(order_q) || pos_lt(peek(reord_q)->base, dq_get(order_q, 0).base)) {
    Trace(("Rejected bogus block at {%u}", nbsx2(peek(reord_q)->base)));
    free(dequeue(reord_q));
    out_slots++;
    check_invariants();
    return;
  }

  ord = shift(order_q);
  oblk = dequeue(reord_q);

  offs_incr = (reord_offs < oblk->end_offset ?
               oblk->end_offset - reord_offs : 0u);
  reord_offs += offs_incr;

  if (oblk->blk_sz > ord.hdr.bs100k * 100000u)
    oblk->status = ERR_OVERFLOW;
  if (oblk->status == MORE) {
    ord.base.minor++;
    unshift(order_q, ord);
  }
  else {
    if (oblk->status == OK && oblk->crc != ord.hdr.crc)
      oblk->status = ERR_BLKCRC;
    if (oblk->status != OK)
      failf(&ispec, "compressed data error: %s", err2str(oblk->status));
  }

  sink_write_buffer(oblk + 1, oblk->size, 4 * offs_incr);
  check_invariants();
}


static bool
can_scan(void)
{
  return ((work_units > SCAN_THRESH || (work_units > 0u && !parse_token))
          && !ultra && !empty(scan_q) && can_attach(*peek(scan_q)));
}

static void
do_scan(void)
{
  struct detached_bitstream *bs;
  int scan_result;
  unsigned skip;
  struct bitstream true_bitstream;

  assert(!parsing_done);
  work_units--;
  bs = dequeue(scan_q);

  skip = 0u;
  assert(bs->pos.major >= parser_bs.pos.major);
  if (bs->pos.major == parser_bs.pos.major
      && bs->pos.minor < parser_bs.pos.minor) {
    skip = (parser_bs.pos.minor - bs->pos.minor) >> 27;
  }

  true_bitstream = attach(*bs);
  scan_result = scan(&true_bitstream, skip);
  *bs = detach(true_bitstream);

  if (scan_result != OK || parsing_done) {
    work_units++;
    free(bs);
    check_invariants();
    return;
  }

  if (pos_le(bs->pos, parser_bs.pos)) {
    Trace(("Scanner found a known pattern at {%lu}",
           32ul + 32ul * bs->offset - bs->live));
    work_units++;
  }
  else {
    struct unord_blk *ub;
    struct retr_blk *rb;

    Trace(("Scanner found a unique match at {%lu}",
           32ul + 32ul * bs->offset - bs->live));

    ub = XMALLOC(struct unord_blk);
    ub->base = bs->pos;
    ub->end_pos = *bs;
    ub->complete = false;
    enqueue(unord_q, ub);

    rb = XMALLOC(struct retr_blk);
    rb->unord_link = ub;
    decoder_init(&rb->ds);
    rb->curr_pos = *bs;
    rb->base = bs->pos;
    enqueue(retr_q, rb);
  }

  if (true_bitstream.data != true_bitstream.limit && bs->offset >= head_offs) {
    enqueue(scan_q, bs);
  }
  else {
    free(bs);
  }

  check_invariants();
}


static bool
can_terminate(void)
{
  return (eof && parsing_done && parse_token
          && work_units == num_worker
          && out_slots == total_out_slots);
}


static void
on_input_avail(void *buffer, size_t size)
{
  struct in_blk *iblk;
  struct detached_bitstream *scan_task;
  unsigned missing;

  iblk = XMALLOC(struct in_blk);;
  iblk->buffer = buffer;
  iblk->size = (size + 3) / 4;
  iblk->ref_count = 1u;
  iblk->offset = tail_offs;
  /* tail_offs is a protected variable, but current thread is the only one that
     can possibly alter it, so here we can read tail_offs without locking. */

  missing = -size % 4;
  bzero((char *)buffer + size, missing);

  scan_task = XMALLOC(struct detached_bitstream);
  *scan_task = bits_init(tail_offs);

  sched_lock();
  if (parsing_done) {
    sched_unlock();
    free(iblk);
    free(scan_task);
    source_release_buffer(buffer);
    return;
  }

  eof_missing = missing;
  tail_offs += iblk->size;
  push(input_q, iblk);
  enqueue(scan_q, scan_task);
  check_invariants();
  sched_unlock();
}


static void
on_write_complete(void *buffer)
{
  struct out_blk *oblk = buffer;

  free(oblk - 1);

  sched_lock();
  ++out_slots;
  check_invariants();
  sched_unlock();
}


static void
init(void)
{
  deque_init(input_q, in_slots);
  pqueue_init(scan_q, in_slots);
  pqueue_init(retr_q, work_units);
  pqueue_init(emit_q, work_units);
  pqueue_init(unord_q, (work_units + out_slots > UNORD_THRESH ?
                        work_units + out_slots - UNORD_THRESH : 0));
  deque_init(order_q, work_units + out_slots);
  pqueue_init(reord_q, out_slots);

  head_offs = 0;
  tail_offs = 0;
  eof_missing = 0;
  parsing_done = false;
  parse_token = true;
  reord_offs = 0;

  parser_bs = bits_init(0);
  parser_init(&par, bs100k);
}


static void
uninit(void)
{
  assert(parsing_done);
  assert(head_offs == tail_offs);
  assert(parse_token);

  pqueue_uninit(scan_q);
  pqueue_uninit(unord_q);
  deque_uninit(order_q);
  pqueue_uninit(reord_q);
  pqueue_uninit(emit_q);
  pqueue_uninit(retr_q);
  deque_uninit(input_q);
}


static const struct task task_list[] = {
  { "reorder",  can_reorder,  do_reorder  },
  { "parse",    can_parse,    do_parse    },
  { "emit",     can_emit,     do_emit     },
  { "retrieve", can_retrieve, do_retrieve },
  { "scan",     can_scan,     do_scan     },
  { NULL,       NULL,         NULL        },
};

const struct process expansion = {
  task_list,
  init,
  uninit,
  can_terminate,
  on_input_avail,
  on_write_complete,
};
