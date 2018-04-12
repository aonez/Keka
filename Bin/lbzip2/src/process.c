/*-
  process.c -- priority scheduling

  Copyright (C) 2012 Mikolaj Izdebski

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

#include <arpa/inet.h>          /* ntohl() */
#include <pthread.h>            /* pthread_t */
#include <signal.h>             /* SIGUSR2 */
#include <unistd.h>             /* write() */

#include "timespec.h"           /* struct timespec */
#include "main.h"               /* work() */

#include "process.h"            /* struct process */
#include "signals.h"            /* halt() */


/*
  JOB SCHEDULING

  Jobs of different importance for the whole process are divided into several
  categories. Each category has a distinct priority assigned to it. Those
  priorities are static -- they were defined at lbzip2 design time and they
  can't change during run time. Any job can be scheduled only if there are no
  available jobs of higher priority. Therefore the primary scheduling
  algorithm of lbzip2 is static priority scheduling.

  The secondary algorithm is EDF (Earliest Deadline First). If two jobs of the
  same priority are to be scheduled (which implies they fall to the same job
  category) their scheduling order is determined basing on their underlying
  blocks order within the bzip2 file. Blocks that need to be outputed sooner
  have respectively higher priority than blocks that can be written later.

  The scheduler maintains several priority queues. Each queue contains jobs of
  the same type, ordered by their deadline. When there is some free computing
  power available (that is a worker thread is idle), the scheduler picks the
  first non-empty priority queue of the highest priority and removes from it
  the job of the earliest deadline. This job is then passed to the worker
  thread, which is executing it.

  The EDF algorithm is proven to be optimal in single-worker configuration.
  In this case it uses the least possible amount of resources (time, memory).
  In case of two or more worker threads much more resources may be needed, but
  the overall time should be no longer than in the case with one worker thread.
*/


/* Error-checking POSIX thread macros.

   For most pthread functions, non-zero return value means a programming bug.
   In this case aborting seems wiser than printing error message and exiting
   because abort() can produce code dumps that can be useful in debugging.
*/
#define xjoin(t)      ((void)(pthread_join((t), NULL)    && (abort(), 0)))
#define xlock(m)      ((void)(pthread_mutex_lock(m)      && (abort(), 0)))
#define xunlock(m)    ((void)(pthread_mutex_unlock(m)    && (abort(), 0)))
#define xwait(c,m)    ((void)(pthread_cond_wait((c),(m)) && (abort(), 0)))
#define xsignal(c)    ((void)(pthread_cond_signal(c)     && (abort(), 0)))
#define xbroadcast(c) ((void)(pthread_cond_broadcast(c)  && (abort(), 0)))

static void *
thread_entry(void *real_entry)
{
  ((void (*)(void))real_entry)();
  return NULL;
}

/* Create a POSIX thread with error checking. */
static pthread_t
xcreate(void (*entry)(void))
{
  int err;
  pthread_t thread;

  err = pthread_create(&thread, NULL, thread_entry, entry);
  if (err != 0)
    failx(err, "unable to create a POSIX thread");

  return thread;
}


void
xread(void *vbuf, size_t *vacant)
{
  char *buffer = vbuf;

  assert(*vacant > 0);

  do {
    ssize_t rd;

    rd = read(ispec.fd, buffer, *vacant > (size_t)SSIZE_MAX ?
              (size_t)SSIZE_MAX : *vacant);

    /* End of file. */
    if (0 == rd)
      break;

    /* Read error. */
    if (-1 == rd) {
      failfx(&ispec, errno, "read()");
    }

    *vacant -= (size_t)rd;
    buffer += (size_t)rd;
    ispec.total += (size_t)rd;
  }
  while (*vacant > 0);
}

void
xwrite(const void *vbuf, size_t size)
{
  const char *buffer = vbuf;

  ospec.total += size;

  if (size > 0 && ospec.fd != -1) {
    do {
      ssize_t wr;

      wr = write(ospec.fd, buffer, size > (size_t)SSIZE_MAX ?
                 (size_t)SSIZE_MAX : size);

      /* Write error. */
      if (-1 == wr) {
        failfx(&ospec, errno, "write()");
      }

      size -= (size_t)wr;
      buffer += (size_t)wr;
    }
    while (size > 0);
  }
}


/* Parent and left child indices. */
#define parent(i) (((i)-1)/2)
#define left(i) ((i)*2+1)

void
up_heap(void *vroot, unsigned size)
{
  struct position **root = vroot;
  struct position *el;
  unsigned j;

  j = size;
  el = root[j];

  while (j > 0 && pos_lt(*el, *root[parent(j)])) {
    root[j] = root[parent(j)];
    j = parent(j);
  }

  root[j] = el;
}

void
down_heap(void *vroot, unsigned size)
{
  struct position **root = vroot;
  struct position *el;
  unsigned j;

  el = root[size];
  root[size] = root[0];

  j = 0;
  while (left(j) < size) {
    unsigned child = left(j);

    if (child + 1 < size && pos_lt(*root[child + 1], *root[child]))
      child++;
    if (pos_le(*el, *root[child]))
      break;
    root[j] = root[child];
    j = child;
  }

  root[j] = el;
}


static pthread_mutex_t source_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  source_cond  = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t sink_mutex   = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  sink_cond    = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t sched_mutex  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  sched_cond   = PTHREAD_COND_INITIALIZER;

static const struct process *process;

bool eof;
unsigned work_units;
unsigned in_slots;
unsigned out_slots;
unsigned total_in_slots;
unsigned total_out_slots;
size_t in_granul;
size_t out_granul;

static bool request_close;

static pthread_t source_thread;
static pthread_t sink_thread;
static pthread_t *worker_thread;


struct block {
  void *buffer;
  size_t size;
  size_t weight;
};

static struct deque(struct block) output_q;
static bool finish;

unsigned thread_id;
/* Highest priority runnable task or NULL if there are no runnable tasks. */
static const struct task *next_task;


static void
source_thread_proc(void)
{
  Trace(("    source: spawned"));

  for (;;) {
    void *buffer;
    size_t vacant, avail;

    xlock(&source_mutex);
    while (in_slots == 0 && !request_close) {
      Trace(("    source: stalled"));
      xwait(&source_cond, &source_mutex);
    }

    if (request_close) {
      Trace(("    source: received premature close requtest"));
      xunlock(&source_mutex);
      break;
    }

    Trace(("    source: reading data (%u free slots)", in_slots));
    in_slots--;
    xunlock(&source_mutex);

    vacant = in_granul;
    avail = vacant;
    buffer = XNMALLOC(vacant, uint8_t);
    xread(buffer, &vacant);
    avail -= vacant;

    Trace(("    source: block of %u bytes read", (unsigned)avail));

    if (avail == 0u)
      source_release_buffer(buffer);
    else
      process->on_block(buffer, avail);

    if (vacant > 0u)
      break;
  }

  sched_lock();
  eof = 1;
  sched_unlock();

  Trace(("    source: terminating"));
}


void
source_release_buffer(void *buffer)
{
  free(buffer);

  xlock(&source_mutex);
  if (in_slots++ == 0)
    xsignal(&source_cond);
  xunlock(&source_mutex);
}


void
source_close(void)
{
  xlock(&source_mutex);
  request_close = true;
  if (in_slots == 0)
    xsignal(&source_cond);
  xunlock(&source_mutex);
}


void
sink_write_buffer(void *buffer, size_t size, size_t weight)
{
  struct block block;

  block.buffer = buffer;
  block.size = size;
  block.weight = weight;

  xlock(&sink_mutex);
  push(output_q, block);
  xsignal(&sink_cond);
  xunlock(&sink_mutex);
}


static void
sink_thread_proc(void)
{
  bool progress_enabled;
  uintmax_t processed;
  struct timespec start_time;
  struct timespec next_time;
  struct timespec update_interval;
  struct block block;
  static const double UPDATE_INTERVAL = 0.1;

  Trace(("      sink: spawned"));

  /* Progress info is displayed only if all the following conditions are met:
     1) the user has specified -v or --verbose option
     2) stderr is connected to a terminal device
     3) the input file is a regular file
     4) the input file is nonempty
   */
  progress_enabled = (verbose && ispec.size > 0 && isatty(STDERR_FILENO));
  processed = 0u;
  gettime(&start_time);
  next_time = start_time;
  update_interval = dtotimespec(UPDATE_INTERVAL);

  for (;;) {
    xlock(&sink_mutex);
    while (empty(output_q) && !finish) {
      Trace(("      sink: stalled"));
      xwait(&sink_cond, &sink_mutex);
    }

    if (empty(output_q))
      break;

    block = shift(output_q);
    xunlock(&sink_mutex);

    Trace(("      sink: writing data (%u bytes)", (unsigned)block.size));
    xwrite(block.buffer, block.size);
    Trace(("      sink: releasing output slot"));
    process->on_written(block.buffer);

    if (progress_enabled) {
      struct timespec time_now;
      double completed, elapsed;

      processed = min(processed + block.weight, ispec.size);

      gettime(&time_now);

      if (timespec_cmp(time_now, next_time) > 0) {
        next_time = timespec_add(time_now, update_interval);
        elapsed = timespectod(timespec_sub(time_now, start_time));
        completed = (double)processed / ispec.size;

        if (elapsed < 5)
          display("progress: %.2f%%\r", 100 * completed);
        else
          display("progress: %.2f%%, ETA: %.0f s    \r",
                  100 * completed, elapsed * (1 / completed - 1));
      }
    }
  }

  xunlock(&sink_mutex);

  Trace(("      sink: terminating"));
}


static void
select_task(void)
{
  const struct task *task;

  for (task = process->tasks; task->ready != NULL; ++task) {
    if (task->ready()) {
      next_task = task;
      return;
    }
  }

  next_task = NULL;
}


static void
worker_thread_proc(void)
{
  unsigned id;

  (void)id;

  xlock(&sched_mutex);
  Trace(("worker[%2u]: spawned", (id = thread_id++)));

  for (;;) {
    while (next_task != NULL) {
      Trace(("worker[%2u]: scheduling task '%s'...", id, next_task->name));
      next_task->run();
      select_task();
    }

    if (process->finished())
      break;

    Trace(("worker[%2u]: stalled", id));
    xwait(&sched_cond, &sched_mutex);
  }

  xbroadcast(&sched_cond);
  xunlock(&sched_mutex);

  Trace(("worker[%2u]: terminating", id));
}


/* Enter scheduler monitor. */
void
sched_lock(void)
{
  xlock(&sched_mutex);
}


/* Leave scheduler monitor. */
void
sched_unlock(void)
{
  select_task();

  if (next_task != NULL || process->finished())
    xsignal(&sched_cond);

  xunlock(&sched_mutex);
}


static void
init_io(void)
{
  request_close = false;
  finish = false;
  deque_init(output_q, out_slots);

  sink_thread = xcreate(sink_thread_proc);
  source_thread = xcreate(source_thread_proc);
}


static void
uninit_io(void)
{
  xjoin(source_thread);

  xlock(&sink_mutex);
  finish = true;
  xsignal(&sink_cond);
  xunlock(&sink_mutex);

  xjoin(sink_thread);
  deque_uninit(output_q);
}


static void
primary_thread(void)
{
  unsigned i;

  thread_id = 0;

  eof = false;
  in_slots = total_in_slots;
  out_slots = total_out_slots;
  work_units = num_worker;

  process->init();
  select_task();
  init_io();

  for (i = 1u; i < num_worker; ++i)
    worker_thread[i] = xcreate(worker_thread_proc);

  worker_thread_proc();

  for (i = 1u; i < num_worker; ++i)
    xjoin(worker_thread[i]);

  uninit_io();
  process->uninit();

  assert(eof);
  assert(in_slots == total_in_slots);
  assert(out_slots == total_out_slots);
  assert(work_units == num_worker);

  xraise(SIGUSR2);
}


static void
copy_on_input_avail(void *buffer, size_t size)
{
  sched_lock();
  out_slots--;
  sched_unlock();

  sink_write_buffer(buffer, size, size);
}


static void
copy_on_write_complete(void *buffer)
{
  source_release_buffer(buffer);

  sched_lock();
  out_slots++;
  sched_unlock();
}


static bool
copy_terminate(void)
{
  if (eof && out_slots == total_out_slots)
    xraise(SIGUSR2);

  return false;
}

static void
copy(void)
{
  static const struct task null_task = { NULL, NULL, NULL };

  static const struct process pseudo_process = {
    &null_task,
    NULL,
    NULL,
    copy_terminate,
    copy_on_input_avail,
    copy_on_write_complete,
  };

  eof = false;
  in_slots = 2;
  out_slots = 2;
  total_out_slots = 2;
  in_granul = 65536;

  process = &pseudo_process;
  init_io();
  halt();
  uninit_io();
}


static void
schedule(const struct process *proc)
{
  process = proc;

  worker_thread = XNMALLOC(num_worker, pthread_t);
  *worker_thread = xcreate(primary_thread);
  halt();
  xjoin(*worker_thread);
  free(worker_thread);
}


/* TODO: Support -m switch */
static void
set_memory_constraints(void)
{
  if (!decompress) {
    total_in_slots = 2u * num_worker;
    total_out_slots = 2u * num_worker + /*TRANSM_THRESH*/2;
    in_granul = bs100k * 100000u;
    out_granul = -1;            /* ignored during compression */
  }
  else if (!small) {
    total_in_slots = 4u * num_worker;
    total_out_slots = 16u * num_worker;
    in_granul = 256u * 1024u;
    out_granul = MAX_BLOCK_SIZE;
  }
  else {
    total_in_slots = 2u;
    total_out_slots = 2u * num_worker;
    in_granul = 32768u;
    out_granul = 900000u;
  }
}


void
work(void)
{
  if (verbose) {
    info(decompress ? "decompressing %s%s%s to %s%s%s" :
         "compressing %s%s%s to %s%s%s", ispec.sep, ispec.fmt,
         ispec.sep, ospec.sep, ospec.fmt, ospec.sep);
  }

  set_memory_constraints();

  if (!decompress) {
    schedule(&compression);
  }
  else {
    uint32_t header;
    size_t vacant = sizeof(header);

    xread(&header, &vacant);

#define MAGIC(k) (0x425A6830u + (k))
    if (vacant == 0 && (ntohl(header) >= MAGIC(1) &&
                        ntohl(header) <= MAGIC(9))) {
      bs100k = ntohl(header) - MAGIC(0);
      schedule(&expansion);
    }
    else if (force && ospec.fd == STDOUT_FILENO) {
      xwrite(&header, sizeof(header) - vacant);
      copy();
    }
    else {
      failf(&ispec, "not a valid bzip2 file");
    }
  }
}
