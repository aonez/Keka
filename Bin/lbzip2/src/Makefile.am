#-
# Copyright (C) 2011, 2012 Mikolaj Izdebski
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
#

AM_CPPFLAGS = -I$(top_srcdir)/lib
AM_CFLAGS = $(WARN_CFLAGS) $(WERROR_CFLAGS)

bin_PROGRAMS = lbzip2

noinst_HEADERS = \
    common.h     \
    decode.h     \
    encode.h     \
    main.h       \
    process.h    \
    scantab.h    \
    signals.h

lbzip2_SOURCES = \
    compress.c   \
    crctab.c     \
    decode.c     \
    divbwt.c     \
    encode.c     \
    expand.c     \
    main.c       \
    parse.c      \
    process.c    \
    signals.c

lbzip2_LDADD = $(top_builddir)/lib/libgnu.a $(LIB_CLOCK_GETTIME) $(LIB_PTHREAD)

install-exec-hook:
	@cd '$(DESTDIR)$(bindir)' && for prog in lbunzip2 lbzcat; do \
    rm -f $$prog$(EXEEXT) && $(LN_S) lbzip2$(EXEEXT) $$prog$(EXEEXT); done

uninstall-hook:
	@cd '$(DESTDIR)$(bindir)' && for prog in lbunzip2 lbzcat; do \
    rm -f $$prog$(EXEEXT); done
