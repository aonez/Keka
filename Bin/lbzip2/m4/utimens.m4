dnl Copyright (C) 2003-2014 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

dnl serial 7

AC_DEFUN([gl_UTIMENS],
[
  dnl Prerequisites of lib/utimens.c.
  AC_REQUIRE([gl_FUNC_UTIMES])
  AC_REQUIRE([gl_CHECK_TYPE_STRUCT_TIMESPEC])
  AC_REQUIRE([gl_CHECK_TYPE_STRUCT_UTIMBUF])
  AC_REQUIRE([AC_CANONICAL_HOST]) dnl for cross-compiles
  AC_CHECK_FUNCS_ONCE([futimes futimesat futimens utimensat lutimes])

  if test $ac_cv_func_futimens = no && test $ac_cv_func_futimesat = yes; then
    dnl FreeBSD 8.0-rc2 mishandles futimesat(fd,NULL,time).  It is not
    dnl standardized, but Solaris implemented it first and uses it as
    dnl its only means to set fd time.
    AC_CACHE_CHECK([whether futimesat handles NULL file],
      [gl_cv_func_futimesat_works],
      [touch conftest.file
       AC_RUN_IFELSE([AC_LANG_PROGRAM([[
#include <stddef.h>
#include <sys/times.h>
#include <fcntl.h>
]], [[    int fd = open ("conftest.file", O_RDWR);
          if (fd < 0) return 1;
          if (futimesat (fd, NULL, NULL)) return 2;
        ]])],
        [gl_cv_func_futimesat_works=yes],
        [gl_cv_func_futimesat_works=no],
        [case "$host_os" in
                   # Guess yes on glibc systems.
           *-gnu*) gl_cv_func_futimesat_works="guessing yes" ;;
                   # If we don't know, assume the worst.
           *)      gl_cv_func_futimesat_works="guessing no" ;;
         esac
        ])
      rm -f conftest.file])
    case "$gl_cv_func_futimesat_works" in
      *yes) ;;
      *)
        AC_DEFINE([FUTIMESAT_NULL_BUG], [1],
          [Define to 1 if futimesat mishandles a NULL file name.])
        ;;
    esac
  fi
])
