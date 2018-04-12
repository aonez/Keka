/*-
  signals.c -- signal handling

  Copyright (C) 2011, 2012, 2013 Mikolaj Izdebski
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

#include <signal.h>             /* kill() */
#include <pthread.h>            /* pthread_sigmask() */
#include <unistd.h>             /* getpid() */

#include "signals.h"


#define EX_FAIL 1

static void
xempty(sigset_t *set)
{
  if (sigemptyset(set) != 0)
    abort();
}

static void
xadd(sigset_t *set, int sig)
{
  if (sigaddset(set, sig) != 0)
    abort();
}

static void
xmask(int how, const sigset_t *set, sigset_t *oset)
{
  if (pthread_sigmask(how, set, oset) != 0)
    abort();
}

static void
xpending(sigset_t *set)
{
  if (sigpending(set) != 0)
    abort();
}

static bool
xmember(const sigset_t *set, int sig)
{
  unsigned rv;

  /* Cast return value to unsigned to save one comparison. */
  rv = sigismember(set, sig);
  if (rv > 1)
    abort();

  return rv;
}

static void
xaction(int sig, void (*handler)(int))
{
  struct sigaction act;

  act.sa_handler = handler;
  xempty(&act.sa_mask);
  act.sa_flags = 0;

  if (sigaction(sig, &act, NULL) != 0)
    abort();
}


/*
  SIGPIPE and SIGXFSZ will be blocked in all sub-threads during the entire
  lifetime of the process. Any EPIPE or EFBIG write() condition will be
  handled just as before lbzip2-0.23, when these signals were ignored.
  However, starting with lbzip2-0.23, SIGPIPE and/or SIGXFSZ will be
  generated for the offending thread(s) in addition. bailout(), when called
  by such sub-threads in response to EPIPE or EFBIG, or when called by other
  sub-threads failing concurrently (but a bit later) for any other reason,
  will forward (regenerate) the pending signal(s) for the whole process. The
  main thread will unblock these signals right before exiting with EX_FAIL in
  bailout(). 2010-03-03 lacos
*/
static const int blocked_signals[] = {
  SIGPIPE,
  SIGXFSZ,
};

static const int handled_signals[] = {
  SIGUSR1,
  SIGUSR2,
  SIGINT,
  SIGTERM,
};

/* Iterate over signal table. */
#define foreach(p,tab) for ((p) = (tab);                                \
                            (p) < (tab) + sizeof(tab) / sizeof(*(tab)); \
                            (p)++)

/* sig_atomic_t is nowhere required to be able to hold signal values. */
static volatile sig_atomic_t caught_index;

static void
signal_handler(int caught)
{
  const int *sig;

  foreach (sig, handled_signals) {
    if (*sig == caught) {
      caught_index = sig - handled_signals;
      return;
    }
  }

  abort();
}


static pid_t pid;
static pthread_t main_thread;
static sigset_t blocked;
static sigset_t handled;
static sigset_t saved;


void
setup_signals(void)
{
  const int *sig;

  pid = getpid();
  main_thread = pthread_self();

  xempty(&blocked);
  foreach (sig, blocked_signals)
    xadd(&blocked, *sig);

  xempty(&handled);
  foreach (sig, handled_signals)
    xadd(&handled, *sig);

  xmask(SIG_BLOCK, &blocked, 0);
}


/* Block signals. */
void
cli(void)
{
  const int *sig;

  xmask(SIG_BLOCK, &handled, &saved);

  foreach (sig, handled_signals)
    xaction(*sig, signal_handler);
}


/* Unblock signals. */
void
sti(void)
{
  const int *sig;

  foreach (sig, handled_signals)
    xaction(*sig, SIG_DFL);

  xmask(SIG_UNBLOCK, &handled, NULL);
}


/* Terminate the process with a signal. */
static void _Noreturn
terminate(int sig)
{
  sigset_t set;

  /* We might have inherited a SIG_IGN from the parent, but that would make no
     sense here. 24-OCT-2009 lacos */
  xaction(sig, SIG_DFL);
  xraise(sig);

  xempty(&set);
  xadd(&set, sig);
  xmask(SIG_UNBLOCK, &set, NULL);

  _exit(EX_FAIL);
}


/* Unblock signals, wait for them, then block them again. */
void
halt(void)
{
  int sig;
  int ret;

  /*
     We could wait for signals with either sigwait() or sigsuspend(). SUSv2
     states about sigwait() that its effect on signal actions is unspecified.
     SUSv3 still claims the same.

     The SUSv2 description of sigsuspend() talks about both the thread and the
     whole process being suspended until a signal arrives, although thread
     suspension seems much more likely from the wording. They note that they
     filed a clarification request for this. SUSv3 cleans this up and chooses
     thread suspension which was more logical anyway.

     I favor sigsuspend() because I need to re-raise SIGTERM and SIGINT, and
     unspecified action behavior with sigwait() seems messy.

     13-OCT-2009 lacos
   */
  ret = sigsuspend(&saved);
  assert(-1 == ret && EINTR == errno);

  sig = handled_signals[caught_index];
  assert(xmember(&handled, sig));

  switch (sig) {
  default:
    cleanup();
    terminate(sig);

  case SIGUSR1:
    /* Error from a non-main thread via bailout(). */
    bailout();

  case SIGUSR2:
    /* Muxer thread joined other sub-threads and finished successfully. */
    break;
  }
}


void
xraise(int sig)
{
  if (kill(pid, sig) != 0)
    abort();
}


/* Promote signals pending on current thread to the process level. */
static void
promote(void)
{
  const int *sig;
  sigset_t pending;

  xempty(&pending);
  xpending(&pending);

  foreach (sig, blocked_signals)
    if (xmember(&pending, *sig))
      xraise(*sig);
}


/*
  The treatment for fatal errors.

  If called from the main thread, remove any current output file and bail out.
  Primarily by unblocking any pending SIGPIPE/SIGXFSZ signals; both those
  forwarded by any sub-thread to the process level, and those generated for the
  main thread specifically, in response to an EPIPE/EFBIG write() condition. If
  no such signal is pending, or SIG_IGN was inherited through exec() as their
  actions, then bail out with the failure exit status.

  If called (indirectly) from any other thread, resignal the process with any
  pending SIGPIPE/SIGXFSZ. This will promote any such signal to the process
  level, if it was originally generated for the calling thread, accompanying an
  EPIPE/EFBIG write() condition. If the pending signal was already pending on
  the whole process, this will result in an idempotent kill(). Thereafter, send
  SIGUSR1 to the process, in order to signal the fatal error in the sub-thread.
  Finally, terminate the thread.
*/
void
bailout(void)
{
  if (pthread_equal(pthread_self(), main_thread)) {
    cleanup();
    xmask(SIG_UNBLOCK, &blocked, NULL);
    _exit(EX_FAIL);
  }

  promote();
  xraise(SIGUSR1);
  pthread_exit(NULL);
}
