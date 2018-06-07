/*-
  process.h -- priority scheduling header

  Copyright (C) 2012, 2013 Mikolaj Izdebski

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

/* Task - smallest schedulable unit.
*/
struct task {
  const char *name;     /* task name, used only for debugging */
  bool (*ready)(void);  /* returns true iff task is runnable */
  void (*run)(void);    /* completes the task */
};

/* Process - a group of tasks that share a single chunked input and a single
   chunked output.
*/
struct process {
  const struct task *tasks;  /* list of tasks ended by null entry */
  void (*init)(void);        /* called once before process is started */
  void (*uninit)(void);      /* called once after process finishes */
  bool (*finished)(void);    /* returns true iff stop condition was reached */
  void (*on_block)(void *buffer, size_t size);   /* inout block is available */
  void (*on_written)(void *buffer);              /* outout block was written */
};

/* Unique position in compressed stream.  This limits maximal supported stream
   size to about 19 YB (yottabytes).
*/
struct position {
  uint64_t major;  /* I/O block ordinal number */
  uint64_t minor;  /* bit offset within I/O block */
};

/* Generic priority queue implemented as binary heap.  Elements are inserted
   into queue based on their priority.  Only element at head can be accessed or
   removed.  Operations modifying queue run in O(log n) time, where n is queue
   size.  Read-only operations are O(1) time.  Priority queue has constant
   capacity, set at initialization time.
*/
#define pqueue(T)                               \
  {                                             \
    T *restrict root;                           \
    unsigned size;                              \
  }

/* Generic deque implemented as ring buffer.  Elements can be added and removed
   at both ends, and can be accessed at arbitrary indexes.  All operations are
   O(1) time.  Deque has constant capacity, set at initialization time.
*/
#define deque(T)                                \
  {                                             \
    T *restrict root;                           \
    unsigned size;                              \
    unsigned modulus;                           \
    unsigned head;                              \
  }


extern bool eof;                   /* true iff end of input was reached */
extern unsigned work_units;        /* number of available work units */
extern unsigned in_slots;          /* number of available input slots */
extern unsigned out_slots;         /* number of available output slots */
extern unsigned total_work_units;  /* total number of work units */
extern unsigned total_in_slots;    /* total number of input slots */
extern unsigned total_out_slots;   /* total number of output slots */
extern size_t in_granul;           /* size of input I/O block in bytes */
extern size_t out_granul;          /* size of output I/O block in bytes */

extern const struct process compression;
extern const struct process expansion;


/* Compare two positions `a' and `b'.  Return 1 if `a' == `b', 0 otherwise. */
#define pos_eq(a,b) ((a).major == (b).major &&  \
                     (a).minor == (b).minor)

/* Compare two positions `a' and `b'.  Return 1 if `a' < `b', 0 otherwise. */
#define pos_lt(a,b) ((a).major < (b).major ||                           \
                     ((a).major == (b).major && (a).minor < (b).minor))

/* Compare two positions `a' and `b'.  Return 1 if `a' <= `b', 0 otherwise. */
#define pos_le(a,b) (!pos_lt(b,a))

/* Initialize priority queue `q' with no content and capacity `n'. */
#define pqueue_init(q,n) ((q).root = xmalloc((n) * sizeof(*(q).root)),  \
                          (void)((q).size = 0))

/* Destroy empty priority queue `q'. */
#define pqueue_uninit(q) (assert(empty(q)), free((q).root))

/* Return head of non-empty priority queue `q'. */
#define peek(q) (*(q).root)

/* Insert element `e' into non-full priority queue `q'. */
#define enqueue(q,e) ((q).root[(q).size] = (e),         \
                      up_heap((q).root, (q).size++))

/* Remove and return head of non-empty priority queue `q'. */
#define dequeue(q) (down_heap((q).root, --(q).size),    \
                    (q).root[(q).size])

/* Initialize deque `q' with no content and capacity `n'. */
#define deque_init(q,n) ((q).root = xmalloc((n) * sizeof(*(q).root)),   \
                         (q).modulus = (n),                             \
                         (void)((q).head = (q).size = 0))

/* Destroy empty deque `q'. */
#define deque_uninit(q) (assert(empty(q)), free((q).root))

/* Return size of priority queue or deque `q'. */
#define size(q) (+(q).size)

/* Return 1 if priorityy queue or deque `q' is empty, 0 otherwise. */
#define empty(q) (!(q).size)

/* Return element of deque `q' at index `i'.  Size of `q' must be
   greater than `i'. */
#define dq_get(q,i) (assert((i) < (q).size),                            \
                     (q).root[min((q).head + (i) + 1,                   \
                                  (q).head + (i) + 1 - (q).modulus)])

/* Set element of deque `q' at index `i' to `e`.  Size of `q' must be
   greater than `i'.  Return `e'. */
#define dq_set(q,i,e) (assert((i) < (q).size),                          \
                       (q).root[min((q).head + (i) + 1,                 \
                                    (q).head + (i) + 1 - (q).modulus)] = (e))

/* Remove and return head of non-empty deque `q'. */
#define shift(q) (assert(!empty(q)),                                    \
                  (q).size--,                                           \
                  (q).head = min((q).head + 1u,                         \
                                 (q).head + 1u - (q).modulus),          \
                  (q).root[(q).head])

/* Insert element `e' in front of non-full deque `q'. */
#define unshift(q,e) (assert((q).size < (q).modulus),                   \
                      (q).size++,                                       \
                      (q).root[(q).head] = (e),                         \
                      (q).head = min((q).head - 1,                      \
                                     (q).head - 1 + (q).modulus),       \
                      (void)0)

/* Insert element `e' at the end of non-full deque `q'. */
#define push(q,e) (assert((q).size < (q).modulus),                      \
                   (q).size++,                                          \
                   (q).root[min((q).head + (q).size,                    \
                                (q).head + (q).size - (q).modulus)] = (e))

/* Remove and return tail of non-empty deque `q'. */
#define pop(q) (assert(!empty(q)),                                      \
                (q).size--,                                             \
                (q).root[min((q).head + (q).size + 1,                   \
                             (q).head + (q).size + 1 - (q).modulus)])


/* Enter scheduler monitor. */
void sched_lock(void);

/* Leave scheduler monitor. */
void sched_unlock(void);

/* Send asynchronous message to reader thread requesting it to prematurely
   close input stream.  Execution of the request may be postponed
   indeterminately until any pending read operation completes. */
void source_close(void);

/* Send asynchronous message to reader thread notifying it that given I/O block
   is not needed any longer so that it can be released or reused. */
void source_release_buffer(void *buffer);

/* Send asynchronous mesage to writer thread requesting it to write specified
   I/O block to output stream.  Requests are processed in order of arrival.
   Weight is used only for progress monitoring. */
void sink_write_buffer(void *buffer, size_t size, size_t weight);

/* Synchronously read from 0 to `*vacant' bytes from input stream.  `vacant' is
   updated to hold number of unused bytes in the buffer.  If uppon return
   `vacant' is non-zero then end of file was reached.  I/O errors are handled
   internally.  Thread-unsafe. */
void xread(void *vbuf, size_t *vacant);

/* Synchronously write the whole buffer to output stream.  I/O errors are
   handled internally.  Thread-unsafe. */
void xwrite(const void *vbuf, size_t size);

/* Private binary heap manipulation helper functions, used internally
   by priority queue macros. */
void up_heap(void *root, unsigned size);
void down_heap(void *root, unsigned size);
