# gettime.m4 serial 8
dnl Copyright (C) 2002, 2004-2006, 2009-2014 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

AC_DEFUN([gl_GETTIME],
[
  dnl Prerequisites of lib/gettime.c.
  AC_REQUIRE([gl_CLOCK_TIME])
  AC_REQUIRE([gl_TIMESPEC])
  AC_CHECK_FUNCS_ONCE([gettimeofday nanotime])
])
