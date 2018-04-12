/* Set file access and modification times.

   Copyright (C) 2003-2014 Free Software Foundation, Inc.

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

/* derived from a function in touch.c */

#include <config.h>

#define _GL_UTIMENS_INLINE _GL_EXTERN_INLINE
#include "utimens.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "stat-time.h"
#include "timespec.h"

#if HAVE_UTIME_H
# include <utime.h>
#endif

/* Some systems (even some that do have <utime.h>) don't declare this
   structure anywhere.  */
#ifndef HAVE_STRUCT_UTIMBUF
struct utimbuf
{
  long actime;
  long modtime;
};
#endif

/* Avoid recursion with rpl_futimens or rpl_utimensat.  */
#undef futimens
#undef utimensat

/* Solaris 9 mistakenly succeeds when given a non-directory with a
   trailing slash.  Force the use of rpl_stat for a fix.  */
#ifndef REPLACE_FUNC_STAT_FILE
# define REPLACE_FUNC_STAT_FILE 0
#endif

#if HAVE_UTIMENSAT || HAVE_FUTIMENS
/* Cache variables for whether the utimensat syscall works; used to
   avoid calling the syscall if we know it will just fail with ENOSYS,
   and to avoid unnecessary work in massaging timestamps if the
   syscall will work.  Multiple variables are needed, to distinguish
   between the following scenarios on Linux:
   utimensat doesn't exist, or is in glibc but kernel 2.6.18 fails with ENOSYS
   kernel 2.6.22 and earlier rejects AT_SYMLINK_NOFOLLOW
   kernel 2.6.25 and earlier reject UTIME_NOW/UTIME_OMIT with non-zero tv_sec
   kernel 2.6.32 used with xfs or ntfs-3g fail to honor UTIME_OMIT
   utimensat completely works
   For each cache variable: 0 = unknown, 1 = yes, -1 = no.  */
static int utimensat_works_really;
static int lutimensat_works_really;
#endif /* HAVE_UTIMENSAT || HAVE_FUTIMENS */

/* Validate the requested timestamps.  Return 0 if the resulting
   timespec can be used for utimensat (after possibly modifying it to
   work around bugs in utimensat).  Return a positive value if the
   timespec needs further adjustment based on stat results: 1 if any
   adjustment is needed for utimes, and 2 if any adjustment is needed
   for Linux utimensat.  Return -1, with errno set to EINVAL, if
   timespec is out of range.  */
static int
validate_timespec (struct timespec timespec[2])
{
  int result = 0;
  int utime_omit_count = 0;
  assert (timespec);
  if ((timespec[0].tv_nsec != UTIME_NOW
       && timespec[0].tv_nsec != UTIME_OMIT
       && ! (0 <= timespec[0].tv_nsec
             && timespec[0].tv_nsec < TIMESPEC_RESOLUTION))
      || (timespec[1].tv_nsec != UTIME_NOW
          && timespec[1].tv_nsec != UTIME_OMIT
          && ! (0 <= timespec[1].tv_nsec
                && timespec[1].tv_nsec < TIMESPEC_RESOLUTION)))
    {
      errno = EINVAL;
      return -1;
    }
  /* Work around Linux kernel 2.6.25 bug, where utimensat fails with
     EINVAL if tv_sec is not 0 when using the flag values of tv_nsec.
     Flag a Linux kernel 2.6.32 bug, where an mtime of UTIME_OMIT
     fails to bump ctime.  */
  if (timespec[0].tv_nsec == UTIME_NOW
      || timespec[0].tv_nsec == UTIME_OMIT)
    {
      timespec[0].tv_sec = 0;
      result = 1;
      if (timespec[0].tv_nsec == UTIME_OMIT)
        utime_omit_count++;
    }
  if (timespec[1].tv_nsec == UTIME_NOW
      || timespec[1].tv_nsec == UTIME_OMIT)
    {
      timespec[1].tv_sec = 0;
      result = 1;
      if (timespec[1].tv_nsec == UTIME_OMIT)
        utime_omit_count++;
    }
  return result + (utime_omit_count == 1);
}

/* Normalize any UTIME_NOW or UTIME_OMIT values in *TS, using stat
   buffer STATBUF to obtain the current timestamps of the file.  If
   both times are UTIME_NOW, set *TS to NULL (as this can avoid some
   permissions issues).  If both times are UTIME_OMIT, return true
   (nothing further beyond the prior collection of STATBUF is
   necessary); otherwise return false.  */
static bool
update_timespec (struct stat const *statbuf, struct timespec *ts[2])
{
  struct timespec *timespec = *ts;
  if (timespec[0].tv_nsec == UTIME_OMIT
      && timespec[1].tv_nsec == UTIME_OMIT)
    return true;
  if (timespec[0].tv_nsec == UTIME_NOW
      && timespec[1].tv_nsec == UTIME_NOW)
    {
      *ts = NULL;
      return false;
    }

  if (timespec[0].tv_nsec == UTIME_OMIT)
    timespec[0] = get_stat_atime (statbuf);
  else if (timespec[0].tv_nsec == UTIME_NOW)
    gettime (&timespec[0]);

  if (timespec[1].tv_nsec == UTIME_OMIT)
    timespec[1] = get_stat_mtime (statbuf);
  else if (timespec[1].tv_nsec == UTIME_NOW)
    gettime (&timespec[1]);

  return false;
}

/* Set the access and modification time stamps of FD (a.k.a. FILE) to be
   TIMESPEC[0] and TIMESPEC[1], respectively.
   FD must be either negative -- in which case it is ignored --
   or a file descriptor that is open on FILE.
   If FD is nonnegative, then FILE can be NULL, which means
   use just futimes (or equivalent) instead of utimes (or equivalent),
   and fail if on an old system without futimes (or equivalent).
   If TIMESPEC is null, set the time stamps to the current time.
   Return 0 on success, -1 (setting errno) on failure.  */

int
fdutimens (int fd, char const *file, struct timespec const timespec[2])
{
  struct timespec adjusted_timespec[2];
  struct timespec *ts = timespec ? adjusted_timespec : NULL;
  int adjustment_needed = 0;
  struct stat st;

  if (ts)
    {
      adjusted_timespec[0] = timespec[0];
      adjusted_timespec[1] = timespec[1];
      adjustment_needed = validate_timespec (ts);
    }
  if (adjustment_needed < 0)
    return -1;

  /* Require that at least one of FD or FILE are potentially valid, to avoid
     a Linux bug where futimens (AT_FDCWD, NULL) changes "." rather
     than failing.  */
  if (fd < 0 && !file)
    {
      errno = EBADF;
      return -1;
    }

  /* Some Linux-based NFS clients are buggy, and mishandle time stamps
     of files in NFS file systems in some cases.  We have no
     configure-time test for this, but please see
     <http://bugs.gentoo.org/show_bug.cgi?id=132673> for references to
     some of the problems with Linux 2.6.16.  If this affects you,
     compile with -DHAVE_BUGGY_NFS_TIME_STAMPS; this is reported to
     help in some cases, albeit at a cost in performance.  But you
     really should upgrade your kernel to a fixed version, since the
     problem affects many applications.  */

#if HAVE_BUGGY_NFS_TIME_STAMPS
  if (fd < 0)
    sync ();
  else
    fsync (fd);
#endif

  /* POSIX 2008 added two interfaces to set file timestamps with
     nanosecond resolution; newer Linux implements both functions via
     a single syscall.  We provide a fallback for ENOSYS (for example,
     compiling against Linux 2.6.25 kernel headers and glibc 2.7, but
     running on Linux 2.6.18 kernel).  */
#if HAVE_UTIMENSAT || HAVE_FUTIMENS
  if (0 <= utimensat_works_really)
    {
      int result;
# if __linux__ || __sun
      /* As recently as Linux kernel 2.6.32 (Dec 2009), several file
         systems (xfs, ntfs-3g) have bugs with a single UTIME_OMIT,
         but work if both times are either explicitly specified or
         UTIME_NOW.  Work around it with a preparatory [f]stat prior
         to calling futimens/utimensat; fortunately, there is not much
         timing impact due to the extra syscall even on file systems
         where UTIME_OMIT would have worked.

         The same bug occurs in Solaris 11.1 (Apr 2013).

         FIXME: Simplify this for Linux in 2016 and for Solaris in
         2024, when file system bugs are no longer common.  */
      if (adjustment_needed == 2)
        {
          if (fd < 0 ? stat (file, &st) : fstat (fd, &st))
            return -1;
          if (ts[0].tv_nsec == UTIME_OMIT)
            ts[0] = get_stat_atime (&st);
          else if (ts[1].tv_nsec == UTIME_OMIT)
            ts[1] = get_stat_mtime (&st);
          /* Note that st is good, in case utimensat gives ENOSYS.  */
          adjustment_needed++;
        }
# endif
# if HAVE_UTIMENSAT
      if (fd < 0)
        {
          result = utimensat (AT_FDCWD, file, ts, 0);
#  ifdef __linux__
          /* Work around a kernel bug:
             http://bugzilla.redhat.com/442352
             http://bugzilla.redhat.com/449910
             It appears that utimensat can mistakenly return 280 rather
             than -1 upon ENOSYS failure.
             FIXME: remove in 2010 or whenever the offending kernels
             are no longer in common use.  */
          if (0 < result)
            errno = ENOSYS;
#  endif /* __linux__ */
          if (result == 0 || errno != ENOSYS)
            {
              utimensat_works_really = 1;
              return result;
            }
        }
# endif /* HAVE_UTIMENSAT */
# if HAVE_FUTIMENS
      if (0 <= fd)
        {
          result = futimens (fd, ts);
#  ifdef __linux__
          /* Work around the same bug as above.  */
          if (0 < result)
            errno = ENOSYS;
#  endif /* __linux__ */
          if (result == 0 || errno != ENOSYS)
            {
              utimensat_works_really = 1;
              return result;
            }
        }
# endif /* HAVE_FUTIMENS */
    }
  utimensat_works_really = -1;
  lutimensat_works_really = -1;
#endif /* HAVE_UTIMENSAT || HAVE_FUTIMENS */

  /* The platform lacks an interface to set file timestamps with
     nanosecond resolution, so do the best we can, discarding any
     fractional part of the timestamp.  */

  if (adjustment_needed || (REPLACE_FUNC_STAT_FILE && fd < 0))
    {
      if (adjustment_needed != 3
          && (fd < 0 ? stat (file, &st) : fstat (fd, &st)))
        return -1;
      if (ts && update_timespec (&st, &ts))
        return 0;
    }

  {
#if HAVE_FUTIMESAT || HAVE_WORKING_UTIMES
    struct timeval timeval[2];
    struct timeval *t;
    if (ts)
      {
        timeval[0].tv_sec = ts[0].tv_sec;
        timeval[0].tv_usec = ts[0].tv_nsec / 1000;
        timeval[1].tv_sec = ts[1].tv_sec;
        timeval[1].tv_usec = ts[1].tv_nsec / 1000;
        t = timeval;
      }
    else
      t = NULL;

    if (fd < 0)
      {
# if HAVE_FUTIMESAT
        return futimesat (AT_FDCWD, file, t);
# endif
      }
    else
      {
        /* If futimesat or futimes fails here, don't try to speed things
           up by returning right away.  glibc can incorrectly fail with
           errno == ENOENT if /proc isn't mounted.  Also, Mandrake 10.0
           in high security mode doesn't allow ordinary users to read
           /proc/self, so glibc incorrectly fails with errno == EACCES.
           If errno == EIO, EPERM, or EROFS, it's probably safe to fail
           right away, but these cases are rare enough that they're not
           worth optimizing, and who knows what other messed-up systems
           are out there?  So play it safe and fall back on the code
           below.  */

# if (HAVE_FUTIMESAT && !FUTIMESAT_NULL_BUG) || HAVE_FUTIMES
#  if HAVE_FUTIMESAT && !FUTIMESAT_NULL_BUG
#   undef futimes
#   define futimes(fd, t) futimesat (fd, NULL, t)
#  endif
        if (futimes (fd, t) == 0)
          {
#  if __linux__ && __GLIBC__
            /* Work around a longstanding glibc bug, still present as
               of 2010-12-27.  On older Linux kernels that lack both
               utimensat and utimes, glibc's futimes rounds instead of
               truncating when falling back on utime.  The same bug
               occurs in futimesat with a null 2nd arg.  */
            if (t)
              {
                bool abig = 500000 <= t[0].tv_usec;
                bool mbig = 500000 <= t[1].tv_usec;
                if ((abig | mbig) && fstat (fd, &st) == 0)
                  {
                    /* If these two subtractions overflow, they'll
                       track the overflows inside the buggy glibc.  */
                    time_t adiff = st.st_atime - t[0].tv_sec;
                    time_t mdiff = st.st_mtime - t[1].tv_sec;

                    struct timeval *tt = NULL;
                    struct timeval truncated_timeval[2];
                    truncated_timeval[0] = t[0];
                    truncated_timeval[1] = t[1];
                    if (abig && adiff == 1 && get_stat_atime_ns (&st) == 0)
                      {
                        tt = truncated_timeval;
                        tt[0].tv_usec = 0;
                      }
                    if (mbig && mdiff == 1 && get_stat_mtime_ns (&st) == 0)
                      {
                        tt = truncated_timeval;
                        tt[1].tv_usec = 0;
                      }
                    if (tt)
                      futimes (fd, tt);
                  }
              }
#  endif

            return 0;
          }
# endif
      }
#endif /* HAVE_FUTIMESAT || HAVE_WORKING_UTIMES */

    if (!file)
      {
#if ! ((HAVE_FUTIMESAT && !FUTIMESAT_NULL_BUG)          \
        || (HAVE_WORKING_UTIMES && HAVE_FUTIMES))
        errno = ENOSYS;
#endif
        return -1;
      }

#if HAVE_WORKING_UTIMES
    return utimes (file, t);
#else
    {
      struct utimbuf utimbuf;
      struct utimbuf *ut;
      if (ts)
        {
          utimbuf.actime = ts[0].tv_sec;
          utimbuf.modtime = ts[1].tv_sec;
          ut = &utimbuf;
        }
      else
        ut = NULL;

      return utime (file, ut);
    }
#endif /* !HAVE_WORKING_UTIMES */
  }
}

/* Set the access and modification time stamps of FILE to be
   TIMESPEC[0] and TIMESPEC[1], respectively.  */
int
utimens (char const *file, struct timespec const timespec[2])
{
  return fdutimens (-1, file, timespec);
}

/* Set the access and modification time stamps of FILE to be
   TIMESPEC[0] and TIMESPEC[1], respectively, without dereferencing
   symlinks.  Fail with ENOSYS if the platform does not support
   changing symlink timestamps, but FILE was a symlink.  */
int
lutimens (char const *file, struct timespec const timespec[2])
{
  struct timespec adjusted_timespec[2];
  struct timespec *ts = timespec ? adjusted_timespec : NULL;
  int adjustment_needed = 0;
  struct stat st;

  if (ts)
    {
      adjusted_timespec[0] = timespec[0];
      adjusted_timespec[1] = timespec[1];
      adjustment_needed = validate_timespec (ts);
    }
  if (adjustment_needed < 0)
    return -1;

  /* The Linux kernel did not support symlink timestamps until
     utimensat, in version 2.6.22, so we don't need to mimic
     fdutimens' worry about buggy NFS clients.  But we do have to
     worry about bogus return values.  */

#if HAVE_UTIMENSAT
  if (0 <= lutimensat_works_really)
    {
      int result;
# if __linux__ || __sun
      /* As recently as Linux kernel 2.6.32 (Dec 2009), several file
         systems (xfs, ntfs-3g) have bugs with a single UTIME_OMIT,
         but work if both times are either explicitly specified or
         UTIME_NOW.  Work around it with a preparatory lstat prior to
         calling utimensat; fortunately, there is not much timing
         impact due to the extra syscall even on file systems where
         UTIME_OMIT would have worked.

         The same bug occurs in Solaris 11.1 (Apr 2013).

         FIXME: Simplify this for Linux in 2016 and for Solaris in
         2024, when file system bugs are no longer common.  */
      if (adjustment_needed == 2)
        {
          if (lstat (file, &st))
            return -1;
          if (ts[0].tv_nsec == UTIME_OMIT)
            ts[0] = get_stat_atime (&st);
          else if (ts[1].tv_nsec == UTIME_OMIT)
            ts[1] = get_stat_mtime (&st);
          /* Note that st is good, in case utimensat gives ENOSYS.  */
          adjustment_needed++;
        }
# endif
      result = utimensat (AT_FDCWD, file, ts, AT_SYMLINK_NOFOLLOW);
# ifdef __linux__
      /* Work around a kernel bug:
         http://bugzilla.redhat.com/442352
         http://bugzilla.redhat.com/449910
         It appears that utimensat can mistakenly return 280 rather
         than -1 upon ENOSYS failure.
         FIXME: remove in 2010 or whenever the offending kernels
         are no longer in common use.  */
      if (0 < result)
        errno = ENOSYS;
# endif
      if (result == 0 || errno != ENOSYS)
        {
          utimensat_works_really = 1;
          lutimensat_works_really = 1;
          return result;
        }
    }
  lutimensat_works_really = -1;
#endif /* HAVE_UTIMENSAT */

  /* The platform lacks an interface to set file timestamps with
     nanosecond resolution, so do the best we can, discarding any
     fractional part of the timestamp.  */

  if (adjustment_needed || REPLACE_FUNC_STAT_FILE)
    {
      if (adjustment_needed != 3 && lstat (file, &st))
        return -1;
      if (ts && update_timespec (&st, &ts))
        return 0;
    }

  /* On Linux, lutimes is a thin wrapper around utimensat, so there is
     no point trying lutimes if utimensat failed with ENOSYS.  */
#if HAVE_LUTIMES && !HAVE_UTIMENSAT
  {
    struct timeval timeval[2];
    struct timeval *t;
    int result;
    if (ts)
      {
        timeval[0].tv_sec = ts[0].tv_sec;
        timeval[0].tv_usec = ts[0].tv_nsec / 1000;
        timeval[1].tv_sec = ts[1].tv_sec;
        timeval[1].tv_usec = ts[1].tv_nsec / 1000;
        t = timeval;
      }
    else
      t = NULL;

    result = lutimes (file, t);
    if (result == 0 || errno != ENOSYS)
      return result;
  }
#endif /* HAVE_LUTIMES && !HAVE_UTIMENSAT */

  /* Out of luck for symlinks, but we still handle regular files.  */
  if (!(adjustment_needed || REPLACE_FUNC_STAT_FILE) && lstat (file, &st))
    return -1;
  if (!S_ISLNK (st.st_mode))
    return fdutimens (-1, file, ts);
  errno = ENOSYS;
  return -1;
}
