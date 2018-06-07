/*-
  main.h -- main module header

  Copyright (C) 2011, 2012 Mikolaj Izdebski
  Copyright (C) 2008, 2009, 2010 Laszlo Ersek

  This file is part of lbzip2.

  lbzip2 is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  lbzip2 is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with lbzip2.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <limits.h>             /* CHAR_BIT */

#if 8 != CHAR_BIT
#error "Environments where 8 != CHAR_BIT are not supported."
#endif


/*
  The file specifier.

  The pointers "sep" and "fmt" point to character arrays that either don't
  need to be released, or need to be released through different aliases.
  These are prepared solely for logging. This is why the pointed to chars
  are qualified as const.
*/
struct filespec {
  int fd;                       /* the file descriptor; -1 if none */
  const char *sep;              /* name separator; either "" or "\"" */
  const char *fmt;              /* either file name or a special name */
  uintmax_t total;              /* total number of bytes transferred */
  uintmax_t size;               /* file size or 0 if unknown */
};


extern unsigned num_worker;     /* -n */
extern size_t max_mem;          /* -m */
extern bool decompress;         /* -d */
extern unsigned bs100k;         /* -1..-9 */
extern bool force;              /* -f */
extern bool keep;               /* -k */
extern bool verbose;            /* -v */
extern bool print_cctrs;        /* -S */
extern bool small;              /* -s */
extern bool ultra;              /* -u */
extern struct filespec ispec;
extern struct filespec ospec;


void info(const char *fmt, ...)
  __attribute__((format(printf, 1, 2)));
void infof(const struct filespec *f, const char *fmt, ...)
  __attribute__((format(printf, 2, 3)));
void infox(int x, const char *fmt, ...)
  __attribute__((format(printf, 2, 3)));
void infofx(const struct filespec *f, int x, const char *fmt, ...)
  __attribute__((format(printf, 3, 4)));
void warn(const char *fmt, ...)
  __attribute__((format(printf, 1, 2)));
void warnf(const struct filespec *f, const char *fmt, ...)
  __attribute__((format(printf, 2, 3)));
void warnx(int x, const char *fmt, ...)
  __attribute__((format(printf, 2, 3)));
void warnfx(const struct filespec *f, int x, const char *fmt, ...)
  __attribute__((format(printf, 3, 4)));
void _Noreturn fail(const char *fmt, ...)
  __attribute__((format(printf, 1, 2)));
void _Noreturn failf(const struct filespec *f, const char *fmt, ...)
  __attribute__((format(printf, 2, 3)));
void _Noreturn failx(int x, const char *fmt, ...)
  __attribute__((format(printf, 2, 3)));
void _Noreturn failfx(const struct filespec *f, int x, const char *fmt, ...)
  __attribute__((format(printf, 3, 4)));
void display(const char *fmt, ...)
  __attribute__((format(printf, 1, 2)));

void work(void);
