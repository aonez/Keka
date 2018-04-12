/* Set file access and modification times.

   Copyright 2012-2014 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 3 of the License, or any
   later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* Written by Paul Eggert.  */

#include <time.h>
int fdutimens (int, char const *, struct timespec const [2]);
int utimens (char const *, struct timespec const [2]);
int lutimens (char const *, struct timespec const [2]);

#if GNULIB_FDUTIMENSAT
# include <fcntl.h>
# include <sys/stat.h>

#ifndef _GL_INLINE_HEADER_BEGIN
 #error "Please include config.h first."
#endif
_GL_INLINE_HEADER_BEGIN
#ifndef _GL_UTIMENS_INLINE
# define _GL_UTIMENS_INLINE _GL_INLINE
#endif

int fdutimensat (int fd, int dir, char const *name, struct timespec const [2],
                 int atflag);

/* Using this function makes application code slightly more readable.  */
_GL_UTIMENS_INLINE int
lutimensat (int dir, char const *file, struct timespec const times[2])
{
  return utimensat (dir, file, times, AT_SYMLINK_NOFOLLOW);
}

_GL_INLINE_HEADER_END

#endif
