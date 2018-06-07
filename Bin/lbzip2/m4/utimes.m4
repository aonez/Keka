# Detect some bugs in glibc's implementation of utimes.
# serial 3

dnl Copyright (C) 2003-2005, 2009-2014 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

# See if we need to work around bugs in glibc's implementation of
# utimes from 2003-07-12 to 2003-09-17.
# First, there was a bug that would make utimes set mtime
# and atime to zero (1970-01-01) unconditionally.
# Then, there was code to round rather than truncate.
# Then, there was an implementation (sparc64, Linux-2.4.28, glibc-2.3.3)
# that didn't honor the NULL-means-set-to-current-time semantics.
# Finally, there was also a version of utimes that failed on read-only
# files, while utime worked fine (linux-2.2.20, glibc-2.2.5).
#
# From Jim Meyering, with suggestions from Paul Eggert.

AC_DEFUN([gl_FUNC_UTIMES],
[
  AC_CACHE_CHECK([whether the utimes function works],
                 [gl_cv_func_working_utimes],
  [
  AC_RUN_IFELSE([AC_LANG_SOURCE([[
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <utime.h>

static int
inorder (time_t a, time_t b, time_t c)
{
  return a <= b && b <= c;
}

int
main ()
{
  int result = 0;
  char const *file = "conftest.utimes";
  static struct timeval timeval[2] = {{9, 10}, {999999, 999999}};

  /* Test whether utimes() essentially works.  */
  {
    struct stat sbuf;
    FILE *f = fopen (file, "w");
    if (f == NULL)
      result |= 1;
    else if (fclose (f) != 0)
      result |= 1;
    else if (utimes (file, timeval) != 0)
      result |= 2;
    else if (lstat (file, &sbuf) != 0)
      result |= 1;
    else if (!(sbuf.st_atime == timeval[0].tv_sec
               && sbuf.st_mtime == timeval[1].tv_sec))
      result |= 4;
    if (unlink (file) != 0)
      result |= 1;
  }

  /* Test whether utimes() with a NULL argument sets the file's timestamp
     to the current time.  Use 'fstat' as well as 'time' to
     determine the "current" time, to accommodate NFS file systems
     if there is a time skew between the host and the NFS server.  */
  {
    int fd = open (file, O_WRONLY|O_CREAT, 0644);
    if (fd < 0)
      result |= 1;
    else
      {
        time_t t0, t2;
        struct stat st0, st1, st2;
        if (time (&t0) == (time_t) -1)
          result |= 1;
        else if (fstat (fd, &st0) != 0)
          result |= 1;
        else if (utimes (file, timeval) != 0)
          result |= 2;
        else if (utimes (file, NULL) != 0)
          result |= 8;
        else if (fstat (fd, &st1) != 0)
          result |= 1;
        else if (write (fd, "\n", 1) != 1)
          result |= 1;
        else if (fstat (fd, &st2) != 0)
          result |= 1;
        else if (time (&t2) == (time_t) -1)
          result |= 1;
        else
          {
            int m_ok_POSIX = inorder (t0, st1.st_mtime, t2);
            int m_ok_NFS = inorder (st0.st_mtime, st1.st_mtime, st2.st_mtime);
            if (! (st1.st_atime == st1.st_mtime))
              result |= 16;
            if (! (m_ok_POSIX || m_ok_NFS))
              result |= 32;
          }
        if (close (fd) != 0)
          result |= 1;
      }
    if (unlink (file) != 0)
      result |= 1;
  }

  /* Test whether utimes() with a NULL argument works on read-only files.  */
  {
    int fd = open (file, O_WRONLY|O_CREAT, 0444);
    if (fd < 0)
      result |= 1;
    else if (close (fd) != 0)
      result |= 1;
    else if (utimes (file, NULL) != 0)
      result |= 64;
    if (unlink (file) != 0)
      result |= 1;
  }

  return result;
}
  ]])],
       [gl_cv_func_working_utimes=yes],
       [gl_cv_func_working_utimes=no],
       [gl_cv_func_working_utimes=no])])

  if test $gl_cv_func_working_utimes = yes; then
    AC_DEFINE([HAVE_WORKING_UTIMES], [1], [Define if utimes works properly. ])
  fi
])
