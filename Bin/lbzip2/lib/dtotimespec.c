/* Convert double to timespec.

   Copyright (C) 2011-2014 Free Software Foundation, Inc.

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

/* written by Paul Eggert */

/* Convert the double value SEC to a struct timespec.  Round toward
   positive infinity.  On overflow, return an extremal value.  */

#include <config.h>

#include "timespec.h"

#include "intprops.h"

struct timespec
dtotimespec (double sec)
{
  double min_representable = TYPE_MINIMUM (time_t);
  double max_representable =
    ((TYPE_MAXIMUM (time_t) * (double) TIMESPEC_RESOLUTION
      + (TIMESPEC_RESOLUTION - 1))
     / TIMESPEC_RESOLUTION);

  if (! (min_representable < sec))
    return make_timespec (TYPE_MINIMUM (time_t), 0);
  else if (! (sec < max_representable))
    return make_timespec (TYPE_MAXIMUM (time_t), TIMESPEC_RESOLUTION - 1);
  else
    {
      time_t s = sec;
      double frac = TIMESPEC_RESOLUTION * (sec - s);
      long ns = frac;
      ns += ns < frac;
      s += ns / TIMESPEC_RESOLUTION;
      ns %= TIMESPEC_RESOLUTION;

      if (ns < 0)
        {
          s--;
          ns += TIMESPEC_RESOLUTION;
        }

      return make_timespec (s, ns);
    }
}
