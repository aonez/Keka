/* gettime -- get the system clock

   Copyright (C) 2002, 2004-2007, 2009-2014 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* Written by Paul Eggert.  */

#include <config.h>

#include "timespec.h"

#include <sys/time.h>

/* Get the system time into *TS.  */

void
gettime (struct timespec *ts)
{
#if HAVE_NANOTIME
  nanotime (ts);
#else

# if defined CLOCK_REALTIME && HAVE_CLOCK_GETTIME
  if (clock_gettime (CLOCK_REALTIME, ts) == 0)
    return;
# endif

  {
    struct timeval tv;
    gettimeofday (&tv, NULL);
    ts->tv_sec = tv.tv_sec;
    ts->tv_nsec = tv.tv_usec * 1000;
  }

#endif
}
