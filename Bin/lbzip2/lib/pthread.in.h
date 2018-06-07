/* Implement a trivial subset of POSIX 1003.1-2008 pthread.h.

   Copyright (C) 2009-2014 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.  */

/* Written by Paul Eggert and Glen Lenker.  */

#ifndef _@GUARD_PREFIX@_PTHREAD_H_

#if __GNUC__ >= 3
@PRAGMA_SYSTEM_HEADER@
#endif
@PRAGMA_COLUMNS@

/* The include_next requires a split double-inclusion guard.  */
#if @HAVE_PTHREAD_H@
# @INCLUDE_NEXT@ @NEXT_PTHREAD_H@
#endif

#ifndef _@GUARD_PREFIX@_PTHREAD_H_
#define _@GUARD_PREFIX@_PTHREAD_H_

#define __need_system_stdlib_h
#include <stdlib.h>
#undef __need_system_stdlib_h


/* The pthreads-win32 <pthread.h> defines a couple of broken macros.  */
#undef asctime_r
#undef ctime_r
#undef gmtime_r
#undef localtime_r
#undef rand_r
#undef strtok_r

#include <errno.h>
#include <sched.h>
#include <sys/types.h>
#include <time.h>

#ifndef _GL_INLINE_HEADER_BEGIN
 #error "Please include config.h first."
#endif
_GL_INLINE_HEADER_BEGIN
#ifndef _GL_PTHREAD_INLINE
# define _GL_PTHREAD_INLINE _GL_INLINE
#endif

#if ! @HAVE_PTHREAD_T@
# if !GNULIB_defined_pthread_types
 typedef int pthread_t;
 typedef int pthread_attr_t;
 typedef int pthread_barrier_t;
 typedef int pthread_barrierattr_t;
 typedef int pthread_cond_t;
 typedef int pthread_condattr_t;
 typedef int pthread_key_t;
 typedef int pthread_mutex_t;
 typedef int pthread_mutexattr_t;
 typedef int pthread_once_t;
 typedef int pthread_rwlock_t;
 typedef int pthread_rwlockattr_t;
#  define GNULIB_defined_pthread_types 1
# endif
#endif

#ifndef PTHREAD_COND_INITIALIZER
#define PTHREAD_COND_INITIALIZER { 0 }
#define PTHREAD_MUTEX_INITIALIZER { 0 }
#define PTHREAD_ONCE_INIT { 0 }
#define PTHREAD_RWLOCK_INITIALIZER { 0 }

#define PTHREAD_BARRIER_SERIAL_THREAD (-1)

#define PTHREAD_CANCEL_DEFERRED 0
#define PTHREAD_CANCEL_ASYNCHRONOUS 1

#define PTHREAD_CANCEL_ENABLE 0
#define PTHREAD_CANCEL_DISABLE 1

#define PTHREAD_CANCELED ((void *) -1)

#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1

#define PTHREAD_INHERIT_SCHED 0
#define PTHREAD_EXPLICIT_SCHED 1

#define PTHREAD_MUTEX_DEFAULT PTHREAD_MUTEX_NORMAL
#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_MUTEX_ERRORCHECK 1
#define PTHREAD_MUTEX_RECURSIVE 2

#define PTHREAD_MUTEX_STALLED 0
#define PTHREAD_MUTEX_ROBUST 1

#define PTHREAD_PRIO_NONE 0
#define PTHREAD_PRIO_INHERIT 1
#define PTHREAD_PRIO_PROTECT 2

#define PTHREAD_PROCESS_PRIVATE 0
#define PTHREAD_PROCESS_SHARED 1

#define PTHREAD_SCOPE_SYSTEM 0
#define PTHREAD_SCOPE_PROCESS 1
#endif

#if ! @HAVE_PTHREAD_T@

# if !GNULIB_defined_pthread_functions

/* Provide substitutes for the thread functions that should work
   adequately on a single-threaded implementation, where
   pthread_create always fails.  The goal is to let programs compile
   on non-pthread hosts with minimal runtime overhead.

   Omit interfaces that have not been analyzed and for which we do not
   know what to do, so that they elicit a compile-time error for
   now.  */

_GL_PTHREAD_INLINE int
pthread_cond_destroy (pthread_cond_t *cond)
{
  /* COND is never seriously used.  */
  return 0;
}

_GL_PTHREAD_INLINE int
pthread_cond_init (pthread_cond_t *restrict cond,
                   pthread_condattr_t const *restrict attr)
{
  /* COND is never seriously used.  */
  return 0;
}

_GL_PTHREAD_INLINE int
pthread_cond_signal (pthread_cond_t *cond)
{
  /* No threads can currently be blocked on COND.  */
  return 0;
}

_GL_PTHREAD_INLINE int
pthread_cond_wait (pthread_cond_t *restrict cond,
                   pthread_mutex_t *restrict mutex)
{
  /* Properly-written applications never come here.  */
  abort ();
  return 0;
}

_GL_PTHREAD_INLINE int
pthread_create (pthread_t *restrict thread,
                pthread_attr_t const *restrict attr,
                void * (*start_routine) (void*), void *restrict arg)
{
  /* Do not create a thread.  */
  return EAGAIN;
}

_GL_PTHREAD_INLINE void
pthread_exit (void *value)
{
  /* There is just one thread, so the process exits.  */
  exit (0);
}

_GL_PTHREAD_INLINE int
pthread_join (pthread_t thread, void **pvalue)
{
  /* Properly-written applications never come here.  */
  abort ();
  return 0;
}

_GL_PTHREAD_INLINE int
pthread_mutexattr_destroy (pthread_mutexattr_t *attr)
{
  return 0;
}

_GL_PTHREAD_INLINE int
pthread_mutexattr_init (pthread_mutexattr_t *attr)
{
  return 0;
}

_GL_PTHREAD_INLINE int
pthread_mutexattr_settype (pthread_mutexattr_t *attr, int attr_type)
{
  return 0;
}

_GL_PTHREAD_INLINE int
pthread_mutex_destroy (pthread_mutex_t *mutex)
{
  /* MUTEX is never seriously used.  */
  return 0;
}

_GL_PTHREAD_INLINE int
pthread_mutex_init (pthread_mutex_t *restrict mutex,
                    pthread_mutexattr_t const *restrict attr)
{
  /* MUTEX is never seriously used.  */
  return 0;
}

_GL_PTHREAD_INLINE int
pthread_mutex_lock (pthread_mutex_t *mutex)
{
  /* There is only one thread, so it always gets the lock.  This
     implementation does not support PTHREAD_MUTEX_ERRORCHECK.  */
  return 0;
}

_GL_PTHREAD_INLINE int
pthread_mutex_trylock (pthread_mutex_t *mutex)
{
  return pthread_mutex_lock (mutex);
}

_GL_PTHREAD_INLINE int
pthread_mutex_unlock (pthread_mutex_t *mutex)
{
  /* There is only one thread, so it always unlocks successfully.
     This implementation does not support robust mutexes or
     PTHREAD_MUTEX_ERRORCHECK.  */
  return 0;
}

#  define GNULIB_defined_pthread_functions 1
# endif

#endif

#if ! @HAVE_PTHREAD_SPINLOCK_T@

# if !GNULIB_defined_pthread_spinlock_t

/* Approximate spinlocks with mutexes.  */

typedef pthread_mutex_t pthread_spinlock_t;

_GL_PTHREAD_INLINE int
pthread_spin_init (pthread_spinlock_t *lock, int pshared)
{
  return pthread_mutex_init (lock, NULL);
}

_GL_PTHREAD_INLINE int
pthread_spin_destroy (pthread_spinlock_t *lock)
{
  return pthread_mutex_destroy (lock);
}

_GL_PTHREAD_INLINE int
pthread_spin_lock (pthread_spinlock_t *lock)
{
  return pthread_mutex_lock (lock);
}

_GL_PTHREAD_INLINE int
pthread_spin_trylock (pthread_spinlock_t *lock)
{
  return pthread_mutex_trylock (lock);
}

_GL_PTHREAD_INLINE int
pthread_spin_unlock (pthread_spinlock_t *lock)
{
  return pthread_mutex_unlock (lock);
}

#  define GNULIB_defined_pthread_spinlock_t 1
# endif

#endif

_GL_INLINE_HEADER_END

#endif /* _@GUARD_PREFIX@_PTHREAD_H_ */
#endif /* _@GUARD_PREFIX@_PTHREAD_H_ */
