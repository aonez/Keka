# pthread.m4 serial 8
dnl Copyright (C) 2009-2014 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

AC_DEFUN([gl_PTHREAD_CHECK],
[
   AC_REQUIRE([gl_PTHREAD_DEFAULTS])
   gl_CHECK_NEXT_HEADERS([pthread.h])
   if test $ac_cv_header_pthread_h = yes; then
     HAVE_PTHREAD_H=1
     # mingw 3.0 uses winpthreads which installs broken macros via <pthread.h>
     AC_CACHE_CHECK([whether <pthread.h> pollutes the namespace],
      [gl_cv_header_pthread_h_pollution],
      [AC_COMPILE_IFELSE(
        [AC_LANG_PROGRAM(
          [[#include <pthread.h>
            #ifdef strtok_r
            #error
             break me
            #endif
          ]])],
        [gl_cv_header_pthread_h_pollution=no],
        [gl_cv_header_pthread_h_pollution=yes])])
   else
     HAVE_PTHREAD_H=0
   fi

   AC_CHECK_TYPES([pthread_t, pthread_spinlock_t], [], [],
     [AC_INCLUDES_DEFAULT[
      #if HAVE_PTHREAD_H
       #include <pthread.h>
      #endif]])
   if test $ac_cv_type_pthread_t != yes; then
     HAVE_PTHREAD_T=0
   fi
   if test $ac_cv_type_pthread_spinlock_t != yes; then
     HAVE_PTHREAD_SPINLOCK_T=0
   fi

   if test $ac_cv_header_pthread_h != yes ||
      test $ac_cv_type_pthread_t != yes ||
      test $ac_cv_type_pthread_spinlock_t != yes; then
     PTHREAD_H='pthread.h'
     AC_LIBOBJ([pthread])
   elif test $gl_cv_header_pthread_h_pollution = yes; then
     PTHREAD_H=pthread.h
   else
     PTHREAD_H=
   fi
   AC_SUBST([PTHREAD_H])
   AM_CONDITIONAL([GL_GENERATE_PTHREAD_H], [test -n "$PTHREAD_H"])

   LIB_PTHREAD=
   if test $ac_cv_header_pthread_h = yes; then
     dnl We cannot use AC_SEARCH_LIBS here, because on OSF/1 5.1 pthread_join
     dnl is defined as a macro which expands to __phread_join, and libpthread
     dnl contains a definition for __phread_join but none for pthread_join.
     dnl Also, FreeBSD 9 puts pthread_create in libpthread and pthread_join
     dnl in libc, whereas on IRIX 6.5 the reverse is true; so check for both.
     AC_CACHE_CHECK([for library containing pthread_create and pthread_join],
       [gl_cv_lib_pthread],
       [gl_saved_libs=$LIBS
        gl_cv_lib_pthread=
        for gl_lib_prefix in '' '-lpthread'; do
          LIBS="$gl_lib_prefix $gl_saved_libs"
          AC_LINK_IFELSE(
            [AC_LANG_PROGRAM(
               [[#include <pthread.h>
                 void *noop (void *p) { return p; }]],
               [[pthread_t pt;
                 void *arg = 0;
                 pthread_create (&pt, 0, noop, arg);
                 pthread_join (pthread_self (), &arg);]])],
            [if test -z "$gl_lib_prefix"; then
               gl_cv_lib_pthread="none required"
             else
               gl_cv_lib_pthread=$gl_lib_prefix
             fi])
          test -n "$gl_cv_lib_pthread" && break
        done
        LIBS="$gl_saved_libs"
       ])
     if test "$gl_cv_lib_pthread" != "none required"; then
       LIB_PTHREAD="$gl_cv_lib_pthread"
     fi
   fi
   AC_SUBST([LIB_PTHREAD])

   AC_REQUIRE([AC_C_RESTRICT])
])

AC_DEFUN([gl_PTHREAD_DEFAULTS],
[
  dnl Assume proper GNU behavior unless another module says otherwise.
  HAVE_PTHREAD_H=1;              AC_SUBST([HAVE_PTHREAD_H])
  HAVE_PTHREAD_T=1;              AC_SUBST([HAVE_PTHREAD_T])
  HAVE_PTHREAD_SPINLOCK_T=1;     AC_SUBST([HAVE_PTHREAD_SPINLOCK_T])
])
