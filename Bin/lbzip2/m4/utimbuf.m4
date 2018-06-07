# serial 9

# Copyright (C) 1998-2001, 2003-2004, 2007, 2009-2014 Free Software Foundation,
# Inc.
#
# This file is free software; the Free Software Foundation
# gives unlimited permission to copy and/or distribute it,
# with or without modifications, as long as this notice is preserved.

dnl From Jim Meyering

dnl Define HAVE_STRUCT_UTIMBUF if 'struct utimbuf' is declared --
dnl usually in <utime.h>.
dnl Some systems have utime.h but don't declare the struct anywhere.

AC_DEFUN([gl_CHECK_TYPE_STRUCT_UTIMBUF],
[
  AC_CHECK_HEADERS_ONCE([sys/time.h utime.h])
  AC_CACHE_CHECK([for struct utimbuf], [gl_cv_sys_struct_utimbuf],
    [AC_COMPILE_IFELSE(
       [AC_LANG_PROGRAM(
          [[#if HAVE_SYS_TIME_H
             #include <sys/time.h>
            #endif
            #include <time.h>
            #ifdef HAVE_UTIME_H
             #include <utime.h>
            #endif
          ]],
          [[static struct utimbuf x; x.actime = x.modtime;]])],
       [gl_cv_sys_struct_utimbuf=yes],
       [gl_cv_sys_struct_utimbuf=no])])

  if test $gl_cv_sys_struct_utimbuf = yes; then
    AC_DEFINE([HAVE_STRUCT_UTIMBUF], [1],
      [Define if struct utimbuf is declared -- usually in <utime.h>.
       Some systems have utime.h but don't declare the struct anywhere. ])
  fi
])
