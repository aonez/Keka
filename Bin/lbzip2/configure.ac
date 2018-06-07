#-
# Copyright (C) 2011, 2012, 2013, 2014 Mikolaj Izdebski
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

dnl TODO: check for GCC builtins: expect, prefetch, ctz, ctzl, ctzll

AC_PREREQ([2.63])
AC_INIT([lbzip2], [2.5])

AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([lib/config.h])

AM_INIT_AUTOMAKE([1.14 color-tests])
AM_SILENT_RULES([yes])

AC_PROG_LN_S
AC_PROG_RANLIB
AC_PROG_CC_C89

gl_ASSERT_NO_GNULIB_TESTS
gl_ASSERT_NO_GNULIB_POSIXCHECK
gl_EARLY


AC_ARG_ENABLE([tracing],
  [AS_HELP_STRING([--enable-tracing],
      [enable tracing])],
      [AS_VAR_IF([enableval], [yes], [

    AC_DEFINE([ENABLE_TRACING], [1], [Define to enable tracing])
  ])
])

AC_ARG_ENABLE([warnings],
  [AS_HELP_STRING([--enable-warnings],
      [try to enable many C compiler warnings])],
      [AS_VAR_IF([enableval], [yes], [
    m4_foreach([warn], [attributes,
                        bad-function-cast,
                        builtin-macro-redefined,
                        cast-align,
                        cast-qual,
                        coverage-mismatch,
                        declaration-after-statement,
                        disabled-optimization,
                        extra,
                        extra-tokens,
                        float-equal,
                        format-nonliteral,
                        format-security,
                        format-y2k,
                        init-self,
                        inline,
                        invalid-pch,
                        logical-op,
                        long-long,
                        missing-declarations,
                        missing-format-attribute,
                        missing-include-dirs,
                        missing-noreturn,
                        missing-prototypes,
                        mudflap,
                        multichar,
                        nested-externs,
                        old-style-definition,
                        overlength-strings,
                        packed,
                        packed-bitfield-compat,
                        pointer-arith,
                        redundant-decls,
                        shadow,
                        stack-protector,
                        strict-aliasing,
                        strict-prototypes,
                        switch-default,
                        switch-enum,
                        sync-nand,
                        unknown-pragmas,
                        unused,
                        unused-local-typedefs,
                        unused-macros,
                        vla,
                        volatile-register-var,
                        write-strings],
        [m4_set_add([kjn_warn_set], warn)])

    AC_TRY_COMPILE([#ifndef __clang__
                      not clang
                    #endif], [], [gl_WARN_ADD([-Wno-unknown-warning-option])])
    for kjn_warn in '' all extra m4_set_dump([kjn_warn_set], [ ])
    do :
      gl_WARN_ADD([-W$kjn_warn])
    done

    AC_SUBST([WARN_CFLAGS])
  ])
])

gl_INIT

AC_CONFIG_FILES([Makefile src/Makefile lib/Makefile man/Makefile
    tests/Makefile])
AC_OUTPUT


AC_COPYRIGHT([Copyright (C) 2011, 2012 Mikolaj Izdebski

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
])
