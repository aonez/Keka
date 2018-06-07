# Checks for stat-related time functions.

# Copyright (C) 1998-1999, 2001, 2003, 2005-2007, 2009-2014 Free Software
# Foundation, Inc.

# This file is free software; the Free Software Foundation
# gives unlimited permission to copy and/or distribute it,
# with or without modifications, as long as this notice is preserved.

dnl From Paul Eggert.

# st_atim.tv_nsec - Linux, Solaris, Cygwin
# st_atimespec.tv_nsec - FreeBSD, NetBSD, if ! defined _POSIX_SOURCE
# st_atimensec - FreeBSD, NetBSD, if defined _POSIX_SOURCE
# st_atim.st__tim.tv_nsec - UnixWare (at least 2.1.2 through 7.1)

# st_birthtimespec - FreeBSD, NetBSD (hidden on OpenBSD 3.9, anyway)
# st_birthtim - Cygwin 1.7.0+

AC_DEFUN([gl_STAT_TIME],
[
  AC_REQUIRE([gl_USE_SYSTEM_EXTENSIONS])
  AC_CHECK_HEADERS_ONCE([sys/time.h])

  AC_CHECK_MEMBERS([struct stat.st_atim.tv_nsec],
    [AC_CACHE_CHECK([whether struct stat.st_atim is of type struct timespec],
       [ac_cv_typeof_struct_stat_st_atim_is_struct_timespec],
       [AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
          [[
            #include <sys/types.h>
            #include <sys/stat.h>
            #if HAVE_SYS_TIME_H
            # include <sys/time.h>
            #endif
            #include <time.h>
            struct timespec ts;
            struct stat st;
          ]],
          [[
            st.st_atim = ts;
          ]])],
          [ac_cv_typeof_struct_stat_st_atim_is_struct_timespec=yes],
          [ac_cv_typeof_struct_stat_st_atim_is_struct_timespec=no])])
     if test $ac_cv_typeof_struct_stat_st_atim_is_struct_timespec = yes; then
       AC_DEFINE([TYPEOF_STRUCT_STAT_ST_ATIM_IS_STRUCT_TIMESPEC], [1],
         [Define to 1 if the type of the st_atim member of a struct stat is
          struct timespec.])
     fi],
    [AC_CHECK_MEMBERS([struct stat.st_atimespec.tv_nsec], [],
       [AC_CHECK_MEMBERS([struct stat.st_atimensec], [],
          [AC_CHECK_MEMBERS([struct stat.st_atim.st__tim.tv_nsec], [], [],
             [#include <sys/types.h>
              #include <sys/stat.h>])],
          [#include <sys/types.h>
           #include <sys/stat.h>])],
       [#include <sys/types.h>
        #include <sys/stat.h>])],
    [#include <sys/types.h>
     #include <sys/stat.h>])
])

# Check for st_birthtime, a feature from UFS2 (FreeBSD, NetBSD, OpenBSD, etc.)
# and NTFS (Cygwin).
# There was a time when this field was named st_createtime (21 June
# 2002 to 16 July 2002) But that window is very small and applied only
# to development code, so systems still using that configuration are
# not supported.  See revisions 1.10 and 1.11 of FreeBSD's
# src/sys/ufs/ufs/dinode.h.
#
AC_DEFUN([gl_STAT_BIRTHTIME],
[
  AC_REQUIRE([gl_USE_SYSTEM_EXTENSIONS])
  AC_CHECK_HEADERS_ONCE([sys/time.h])
  AC_CHECK_MEMBERS([struct stat.st_birthtimespec.tv_nsec], [],
    [AC_CHECK_MEMBERS([struct stat.st_birthtimensec], [],
      [AC_CHECK_MEMBERS([struct stat.st_birthtim.tv_nsec], [], [],
         [#include <sys/types.h>
          #include <sys/stat.h>])],
       [#include <sys/types.h>
        #include <sys/stat.h>])],
    [#include <sys/types.h>
     #include <sys/stat.h>])
])
