/*-
  main.c -- main module

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

#include "common.h"

#include <unistd.h>             /* unlink() */
#include <signal.h>             /* SIGPIPE */
#include <stdarg.h>             /* va_list */
#include <stdio.h>              /* vfprintf() */
#include <string.h>             /* strcpy() */
#include <sys/stat.h>           /* lstat() */
#include <fcntl.h>              /* open() */

#include "stat-time.h"          /* get_stat_atime() */
#include "utimens.h"            /* fdutimens() */

#include "signals.h"            /* setup_signals() */
#include "main.h"               /* pname */


unsigned num_worker;            /* -n */
size_t max_mem;                 /* -m */
bool decompress;                /* -d */
unsigned bs100k = 9;            /* -1..-9 */
bool force;                     /* -f */
bool keep;                      /* -k */
bool verbose;                   /* -v */
bool print_cctrs;               /* -S */
bool small;                     /* -s */
bool ultra;                     /* -u */
struct filespec ispec;
struct filespec ospec;

#define EX_OK   0
#define EX_WARN 4

static char *opathn;
static const char *pname;
static bool warned;


/* Called just before abnormal program termination. */
void
cleanup(void)
{
  if (opathn != NULL) {
    (void)unlink(opathn);
    /*
       Don't release "opathn" -- the muxer might encounter a write error and
       access *opathn via "ofmt" for error reporting before the main thread
       re-raises the signal here. This is a deliberate leak, but we're on
       our (short) way out anyway. 16-Feb-2010 lacos
     */
    opathn = NULL;
  }
}

/* Called when one of xalloc functions fails. */
void xalloc_die(void)
{
  failx(errno, "xalloc");
}


/* Logging utilities. */

static void log_generic(const struct filespec *fs, int code, const char *fmt,
  va_list args, int nl) __attribute__((format(printf, 3, 0)));

static void
log_generic(const struct filespec *fs, int code, const char *fmt, va_list args,
  int nl)
{
  if (0 > fprintf(stderr, "%s: ", pname)
      || (fs && 0 > fprintf(stderr, "%s%s%s: ", fs->sep, fs->fmt, fs->sep))
      || 0 > vfprintf(stderr, fmt, args)
      || (0 != code && 0 > fprintf(stderr, ": %s", strerror(code)))
      || (nl && 0 > fprintf(stderr, "\n")) || 0 != fflush(stderr))
    bailout();
}


#define DEF(proto, f, x, warn, bail, nl)        \
  void proto                                    \
  {                                             \
    va_list args;                               \
                                                \
    flockfile(stderr);                          \
    va_start(args, fmt);                        \
    if (!bail || (EPIPE != x && EFBIG != x)) {  \
      log_generic(f, x, fmt, args, nl);         \
    }                                           \
                                                \
    if (!bail) {                                \
      va_end(args);                             \
      if (warn)                                 \
        warned = 1;                             \
      funlockfile(stderr);                      \
    }                                           \
    else                                        \
      bailout();                                \
  }

DEF(info   (                                 const char *fmt, ...), 0,0,0,0,1)
DEF(infof  (const struct filespec *f,        const char *fmt, ...), f,0,0,0,1)
DEF(infox  (                          int x, const char *fmt, ...), 0,x,0,0,1)
DEF(infofx (const struct filespec *f, int x, const char *fmt, ...), f,x,0,0,1)
DEF(warn   (                                 const char *fmt, ...), 0,0,1,0,1)
DEF(warnf  (const struct filespec *f,        const char *fmt, ...), f,0,1,0,1)
DEF(warnx  (                          int x, const char *fmt, ...), 0,x,1,0,1)
DEF(warnfx (const struct filespec *f, int x, const char *fmt, ...), f,x,1,0,1)
DEF(fail   (                                 const char *fmt, ...), 0,0,0,1,1)
DEF(failf  (const struct filespec *f,        const char *fmt, ...), f,0,0,1,1)
DEF(failx  (                          int x, const char *fmt, ...), 0,x,0,1,1)
DEF(failfx (const struct filespec *f, int x, const char *fmt, ...), f,x,0,1,1)
DEF(display( /* WITH NO ADVANCING :) */      const char *fmt, ...), 0,0,0,0,0)

#undef DEF


enum outmode {
  OM_STDOUT,                    /* Write all output to stdout, -c. */
  OM_DISCARD,                   /* Discard output, -t. */
  OM_REGF                       /* Write output to files; neither -t nor -c */
};

static enum outmode outmode = OM_REGF;  /* How to store output, -c/-t. */

/* Names of other recognized environment variables. */
static const char *const ev_name[] = { "LBZIP2", "BZIP2", "BZIP" };

/* Separator characters in environment variable values. No escaping. */
static const char envsep[] = " \t";


static uintmax_t
xstrtol(const char *str, int source, uintmax_t lower, uintmax_t upper)
{
  long tmp;
  char *endptr;
  unsigned shift;
  uintmax_t val;
  const char *suffix = "EePpTtGgMmKk";

  if ('\0' == *str)
    goto fail;

  errno = 0;
  tmp = strtol(str, &endptr, 10);
  if (0 != errno || tmp < 0 || (endptr[0] != '\0' && endptr[1] != '\0'))
    goto fail;
  val = tmp;

  endptr = index(suffix, *endptr);
  if (endptr == NULL)
    goto fail;

  shift = (index(suffix, '\0') - endptr + 1) / 2 * 10;

  if (val > (UINTMAX_MAX >> shift))
    goto fail;
  val <<= shift;

  if (val < lower || val > upper) {
  fail:
    fail("failed to parse \"%s\" from \"-%c\" as an integer in [%ju..%ju],"
         " specify \"-h\" for help", str, source, lower, upper);
  }

  return val;
}


/*
  The usage message, displayed when user gives us `--help' option.

  The following macro definition was generated by pretty-usage.pl script.
  To alter the message, simply edit and run pretty-usage.pl. It will patch
  the macro definition automatically.
*/
#define USAGE_STRING "%s%s%s%s%s%s", "Usage:\n1. PROG [-n WTHRS] [-k|-c|-t] [-\
d|-z] [-1 .. -9] [-f] [-v] [-S] {FILE}\n2. PROG -h|-V\n\nRecognized PROG names\
:\n\n  bunzip2, lbunzip2  : Decompress. Forceable with `-d'.\n  bzcat, lbzcat \
     : Decompress to stdout. Forceable with `-cd'.\n  <otherwise>        : Com\
press. Forceable with `-z'.\n\nEnvironment variables:\n\n  LBZIP2, BZIP2,\n  B\
ZIP               : Insert arguments between PROG and the rest of the\n       \
                command line. Tokens are separated by spaces and tabs;\n      \
                ", " no escaping.\n\nOptions:\n\n  -n WTHRS           : Set th\
e number of (de)compressor threads to WTHRS, where\n                       WTH\
RS is a positive integer.\n  -k, --keep         : Don't remove FILE operands. \
Open regular input files\n                       with more than one link.\n  -\
c, --stdout       : Write output to stdout even with FILE operands. Implies\n \
                      `-k'. Incompatible with `-t'.\n  -t, --test         : Te\
st decompression; discard output instead of writing it\n                ", "  \
     to files or stdout. Implies `-k'. Incompatible with\n                    \
   `-c'.\n  -d, --decompress   : Force decompression over the selection by PRO\
G.\n  -z, --compress     : Force compression over the selection by PROG.\n  -1\
 .. -9           : Set the compression block size to 100K .. 900K.\n  --fast  \
           : Alias for `-1'.\n  --best             : Alias for `-9'. This is t\
he default.\n  -f, --force        : Open non-regular input files. Open input f\
iles with more\n                       than one", " link. Try to remove each o\
utput file before\n                       opening it. With `-cd' copy files no\
t in bzip2 format.\n  -s, --small        : Reduce memory usage at cost of perf\
ormance.\n  -u, --sequential   : Perform splitting input blocks sequentially. \
This may\n                       improve compression ratio and decrease CPU us\
age, but\n                       will degrade scalability.\n  -v, --verbose   \
   : Log each (de)compression start to stderr. Display\n                      \
 compression ratio an", "d space savings. Display progress\n                  \
     information if stderr is connected to a terminal.\n  -S                 :\
 Print condition variable statistics to stderr.\n  -q, --quiet,\n  --repetitiv\
e-fast,\n  --repetitive-best,\n  --exponential      : Accepted for compatibili\
ty, otherwise ignored.\n  -h, --help         : Print this help to stdout and e\
xit.\n  -L, --license, -V,\n  --version          : Print version information t\
o stdout and exit.\n\nOperands:\n\n  FILE               : Specify files to com\
p", "ress or decompress. If no FILE is\n                       given, work as \
a filter. FILEs with `.bz2', `.tbz',\n                       `.tbz2' and `.tz2\
' name suffixes will be skipped when\n                       compressing. When\
 decompressing, `.bz2' suffixes will be\n                       removed in out\
put filenames; `.tbz', `.tbz2' and `.tz2'\n                       suffixes wil\
l be replaced by `.tar'; other filenames\n                       will be suffi\
xed with `.out'.\n"

#define HELP_STRING "%s version %s\n%s\n\n%s%s",                        \
    PACKAGE_NAME, PACKAGE_VERSION, "http://lbzip2.org/",                \
    "Copyright (C) 2011, 2012, 2013, 2014 Mikolaj Izdebski\n"           \
    "Copyright (C) 2008, 2009, 2010 Laszlo Ersek\n"                     \
    "\n"                                                                \
    "This program is free software: you can redistribute it and/or modify\n" \
    "it under the terms of the GNU General Public License as published by\n" \
    "the Free Software Foundation, either version 3 of the License, or\n" \
    "(at your option) any later version.\n"                             \
    "\n",                                                               \
    "This program is distributed in the hope that it will be useful,\n" \
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"  \
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"   \
    "GNU General Public License for more details.\n"                    \
    "\n"                                                                \
    "You should have received a copy of the GNU General Public License\nal" \
    "ong with this program.  If not, see <http://www.gnu.org/licenses/>.\n"


static void _Noreturn
usage(void)
{
  if (0 > printf(USAGE_STRING))
    failx(errno, "printf()");
  if (0 != fclose(stdout))
    failx(errno, "fclose(stdout)");
  _exit(EX_OK);
}


static void _Noreturn
version(void)
{
  if (0 > printf(HELP_STRING))
    failx(errno, "printf()");
  if (0 != fclose(stdout))
    failx(errno, "fclose(stdout)");
  _exit(EX_OK);
}


struct arg {
  struct arg *next;
  const char *val;
};


static void
opts_outmode(char ch)
{
  assert('c' == ch || 't' == ch);
  if (('c' == ch ? OM_DISCARD : OM_STDOUT) == outmode) {
    fail("\"-c\" and \"-t\" are incompatible, specify \"-h\" for help");
  }
  if ('c' == ch) {
    outmode = OM_STDOUT;
  }
  else {
    outmode = OM_DISCARD;
    decompress = 1;
  }
}


static void
opts_decompress(char ch)
{
  assert('d' == ch || 'z' == ch);
  decompress = ('d' == ch);
  if (OM_DISCARD == outmode) {
    outmode = OM_REGF;
  }
}


static void
opts_setup(struct arg **operands, size_t argc, char **argv)
{
  struct arg **link_at;
  uintmax_t mx_worker;

  /*
     Create a homogeneous argument list from the recognized environment
     variables and from the command line.
   */
  *operands = 0;
  link_at = operands;
  {
    size_t ofs;

    for (ofs = 0u; ofs < sizeof ev_name / sizeof ev_name[0]; ++ofs) {
      char *ev_val;

      ev_val = getenv(ev_name[ofs]);
      if (0 != ev_val) {
        char *tok;

        for (tok = strtok(ev_val, envsep); 0 != tok; tok = strtok(0, envsep)) {
          struct arg *arg;

          arg = XMALLOC(struct arg);
          arg->next = NULL;
          arg->val = tok;
          *link_at = arg;
          link_at = &arg->next;
        }
      }
    }

    for (ofs = 1u; ofs < argc; ++ofs) {
      struct arg *arg;

      arg = XMALLOC(struct arg);
      arg->next = NULL;
      arg->val = argv[ofs];
      *link_at = arg;
      link_at = &arg->next;
    }
  }


  /* Effectuate option defaults. */
#ifdef _SC_THREAD_THREADS_MAX
  mx_worker = sysconf(_SC_THREAD_THREADS_MAX);
#else
  mx_worker = -1;
#endif
  mx_worker = min(mx_worker, min(UINT_MAX, SIZE_MAX / sizeof(pthread_t)));

  if (strcmp(pname, "bunzip2") == 0 || strcmp(pname, "lbunzip2") == 0) {
    decompress = 1;
  }
  else if (strcmp(pname, "bzcat") == 0 || strcmp(pname, "lbzcat") == 0) {
    outmode = OM_STDOUT;
    decompress = 1;
  }

  /*
     Process and remove all arguments that are options or option arguments. The
     remaining arguments are the operands.
   */
  link_at = operands;
  {
    enum {
      AS_CONTINUE,              /* Continue argument processing. */
      AS_STOP,                  /* Processing finished because of "--". */
      AS_USAGE,                 /* User asked for help. */
      AS_VERSION                /* User asked for version. */
    } args_state;
    struct arg *arg, *next;

    args_state = AS_CONTINUE;
    for (arg = *operands; 0 != arg && AS_CONTINUE == args_state; arg = next) {
      const char *argscan;

      argscan = arg->val;
      if ('-' != *argscan) {
        /* This is an operand, keep it. */
        link_at = &arg->next;
        next = arg->next;
      }
      else {
        /* This argument holds options and possibly an option argument. */
        ++argscan;

        if ('-' == *argscan) {
          ++argscan;

          if ('\0' == *argscan) {
            args_state = AS_STOP;
          }
          else if (0 == strcmp("stdout", argscan)) {
            opts_outmode('c');
          }
          else if (0 == strcmp("test", argscan)) {
            opts_outmode('t');
          }
          else if (0 == strcmp("decompress", argscan)) {
            opts_decompress('d');
          }
          else if (0 == strcmp("compress", argscan)) {
            opts_decompress('z');
          }
          else if (0 == strcmp("fast", argscan)) {
            bs100k = 1;
          }
          else if (0 == strcmp("best", argscan)) {
            bs100k = 9;
          }
          else if (0 == strcmp("force", argscan)) {
            force = 1;
          }
          else if (0 == strcmp("keep", argscan)) {
            keep = 1;
          }
          else if (0 == strcmp("small", argscan)) {
            small = 1;
          }
          else if (0 == strcmp("sequential", argscan)) {
            ultra = 1;
          }
          else if (0 == strcmp("verbose", argscan)) {
            verbose = 1;
          }
          else if (0 == strcmp("help", argscan)) {
            args_state = AS_USAGE;
          }
          else if (0 == strcmp("license", argscan)
                   || 0 == strcmp("version", argscan)) {
            args_state = AS_VERSION;
          }
          else if (0 != strcmp("quiet", argscan)
                   && 0 != strcmp("repetitive-fast", argscan)
                   && 0 != strcmp("repetitive-best", argscan)
                   && 0 != strcmp("exponential", argscan)) {
            fail("unknown option \"%s\", specify \"-h\" for help", arg->val);
          }
        }                       /* long option */
        else {
          int cont;

          cont = 1;
          do {
            int opt;

            opt = *argscan;
            switch (opt) {
            case '\0':
              cont = 0;
              break;

            case 'c':
            case 't':
              opts_outmode(opt);
              break;

            case 'd':
            case 'z':
              opts_decompress(opt);
              break;

            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
              bs100k = opt - '0';
              break;

            case 'f':
              force = 1;
              break;

            case 'k':
              keep = 1;
              break;

            case 's':
              small = 1;
              break;

            case 'u':
              ultra = 1;
              break;

            case 'v':
              verbose = 1;
              break;

            case 'S':
              print_cctrs = 1;
              break;

            case 'q':
              break;

            case 'h':
              args_state = AS_USAGE;
              cont = 0;
              break;

            case 'L':
            case 'V':
              args_state = AS_VERSION;
              cont = 0;
              break;

            case 'n':
            case 'm':
              ++argscan;

              if ('\0' == *argscan) {
                /* Drop this argument, as it wasn't an operand. */
                next = arg->next;
                free(arg);
                *link_at = next;

                /* Move to next argument, which is an option argument. */
                arg = next;
                if (NULL == arg) {
                  fail("option \"-%.1s\" requires an argument,"
                       " specify \"-h\" for help", argscan - 1);
                }
                argscan = arg->val;
              }

              if (opt == 'n')
                num_worker = xstrtol(argscan, opt, 1, mx_worker);
              else
                max_mem = xstrtol(argscan, opt, 1, SIZE_MAX);

              cont = 0;
              break;

            default:
              fail("unknown option \"-%c\", specify \"-h\" for"
                   " help", opt);
            }                   /* switch (*argscan) */

            ++argscan;
          }
          while (cont);
        }                       /* cluster of short options */

        /* This wasn't an operand, drop it. */
        next = arg->next;
        free(arg);
        *link_at = next;
      }                         /* argument holds options */
    }                           /* arguments loop */

    if (AS_USAGE == args_state || AS_VERSION == args_state) {
      for (arg = *operands; 0 != arg; arg = next) {
        next = arg->next;
        free(arg);
      }
      if (AS_USAGE == args_state)
        usage();
      else
        version();
    }
  }                             /* process arguments */


  /* Finalize options. */
  if (OM_REGF == outmode && 0 == *operands) {
    outmode = OM_STDOUT;
  }

  if (decompress) {
    if (0 == *operands && isatty(STDIN_FILENO)) {
      fail("won't read compressed data from a terminal, specify"
           " \"-h\" for help");
    }
  }
  else {
    if (OM_STDOUT == outmode && isatty(STDOUT_FILENO)) {
      fail("won't write compressed data to a terminal, specify"
           " \"-h\" for help");
    }
  }

  if (0u == num_worker) {
#ifdef _SC_NPROCESSORS_ONLN
    long num_online;

    num_online = sysconf(_SC_NPROCESSORS_ONLN);
    if (-1 == num_online) {
      fail("number of online processors unavailable, specify \"-h\" for help");
    }
    assert(1L <= num_online);
    num_worker = min(mx_worker, (unsigned long)num_online);
#else
    fail("WORKER-THREADS not set, specify \"-h\" for help");
#endif
  }
}


/*
  Dual purpose:
  a) Is the current operand already compressed?
  b) What decompressed suffix corresponds to the current compressed suffix?
*/
struct suffix {
  const char *compr;            /* Suffix of compressed file. */
  size_t compr_len;             /* Its length (not size). */
  const char *decompr;          /* Suffix of decompressed file. */
  size_t decompr_len;           /* Its length (not size). */
  int chk_compr;                /* If "compr" is suited for purpose "a". */
};

#define SUF(c, dc, c1) { c, sizeof c - 1u, dc, sizeof dc - 1u, c1 }
static const struct suffix suffix[] = {
  SUF(".bz2", "", 1),
  SUF(".tbz2", ".tar", 1),
  SUF(".tbz", ".tar", 1),
  SUF(".tz2", ".tar", 1),
  SUF("", ".out", 0)
};

#undef SUF


/*
  If "decompr_pathname" is NULL: check if "compr_pathname" has a compressed
  suffix. If "decompr_pathname" is not NULL: allocate and format a pathname
  for storing the decompressed output -- this always returns 1.
*/
static int
suffix_xform(const char *compr_pathname, char **decompr_pathname)
{
  size_t len, ofs;

  len = strlen(compr_pathname);
  for (ofs = 0u; ofs < sizeof suffix / sizeof suffix[0]; ++ofs) {
    if ((suffix[ofs].chk_compr || 0 != decompr_pathname)
        && len >= suffix[ofs].compr_len) {
      size_t prefix_len;

      prefix_len = len - suffix[ofs].compr_len;
      if (0 == strcmp(compr_pathname + prefix_len, suffix[ofs].compr)) {
        if (0 != decompr_pathname) {
          if (SIZE_MAX - prefix_len < suffix[ofs].decompr_len + 1u) {
            fail("\"%s\": size_t overflow in dpn_alloc\n", compr_pathname);
          }
          *decompr_pathname
              = xmalloc(prefix_len + suffix[ofs].decompr_len + 1u);
          (void)memcpy(*decompr_pathname, compr_pathname, prefix_len);
          (void)strcpy(*decompr_pathname + prefix_len, suffix[ofs].decompr);
        }
        return 1;
      }
    }
  }
  assert(0 == decompr_pathname);
  return 0;
}


/*
  If input is unavailable (skipping), return -1.

  Otherwise:
    - return 0,
    - store the file descriptor to read from (might be -1 if discarding),
    - if input is coming from a successfully opened FILE operand, fill in
      "*sbuf" via fstat() -- but "*sbuf" may be modified without this, too,
    - set up "ispec.sep" and "ispec.fmt" for logging; the character arrays
      pointed to by them won't need to be released (or at least not through
      these aliases).
*/
static int
input_init(const struct arg *operand, struct stat *sbuf)
{
  ispec.total = 0u;

  if (0 == operand) {
    ispec.fd = STDIN_FILENO;
    ispec.sep = "";
    ispec.fmt = "stdin";
    ispec.size = 0u;
    return 0;
  }

  if (!force) {
    if (-1 == lstat(operand->val, sbuf)) {
      warnx(errno, "skipping \"%s\": lstat()", operand->val);
      return -1;
    }

    if (OM_REGF == outmode && !S_ISREG(sbuf->st_mode)) {
      warn("skipping \"%s\": not a regular file", operand->val);
      return -1;
    }

    if (OM_REGF == outmode && !keep && sbuf->st_nlink > (nlink_t) 1) {
      warn("skipping \"%s\": more than one links", operand->val);
      return -1;
    }
  }

  if (!decompress && suffix_xform(operand->val, 0)) {
    warn("skipping \"%s\": compressed suffix", operand->val);
  }
  else {
    int infd;

    infd = open(operand->val, O_RDONLY | O_NOCTTY);
    if (-1 == infd) {
      warnx(errno, "skipping \"%s\": open()", operand->val);
    }
    else {
      if (-1 != fstat(infd, sbuf)) {
        ispec.fd = infd;
        ispec.sep = "\"";
        ispec.fmt = operand->val;
        assert(0 <= sbuf->st_size);
        ispec.size = sbuf->st_size;
        return 0;
      }

      warnx(errno, "skipping \"%s\": fstat()", operand->val);
      if (-1 == close(infd)) {
        failx(errno, "close(\"%s\")", operand->val);
      }
    }
  }

  return -1;
}


static void
input_oprnd_rm(const struct arg *operand)
{
  assert(0 != operand);

  if (-1 == unlink(operand->val) && ENOENT != errno) {
    warnx(errno, "unlink(\"%s\")", operand->val);
  }
}


static void
input_uninit(void)
{
  if (-1 == close(ispec.fd)) {
    failx(errno, "close(%s%s%s)", ispec.sep, ispec.fmt, ispec.sep);
  }
}


/*
  If skipping (output unavailable), return -1.

  Otherwise:
    - return 0,
    - store the file descriptor to write to (might be -1 if discarding),
    - if we write to a regular file, store the dynamically allocated output
      pathname,
    - set up "ospec.sep" and "ospec.fmt" for logging; the character arrays
      pointed to by them won't need to be released (or at least not through
      these aliases).
*/
static int
output_init(const struct arg *operand, const struct stat *sbuf)
{
  ospec.total = 0u;

  switch (outmode) {
  case OM_STDOUT:
    ospec.fd = STDOUT_FILENO;
    ospec.sep = "";
    ospec.fmt = "stdout";
    return 0;

  case OM_DISCARD:
    ospec.fd = -1;
    ospec.sep = "";
    ospec.fmt = "the bit bucket";
    return 0;

  case OM_REGF:
    assert(0 != operand);

    {
      char *tmp;

      if (decompress) {
        (void) suffix_xform(operand->val, &tmp);
      }
      else {
        size_t len;

        len = strlen(operand->val);
        if (SIZE_MAX - sizeof ".bz2" < len) {
          fail("\"%s\": size_t overflow in cpn_alloc\n", operand->val);
        }
        tmp = xmalloc(len + sizeof ".bz2");
        (void)memcpy(tmp, operand->val, len);
        (void)strcpy(tmp + len, ".bz2");
      }

      if (force && -1 == unlink(tmp) && ENOENT != errno) {
        /*
           This doesn't warrant a warning in itself, just an explanation if
           the following open() fails.
         */
        infox(errno, "unlink(\"%s\")", tmp);
      }

      ospec.fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL,
                       sbuf->st_mode & (S_IRUSR | S_IWUSR));

      if (-1 == ospec.fd) {
        warnx(errno, "skipping \"%s\": open(\"%s\")", operand->val, tmp);
        free(tmp);
      }
      else {
        opathn = tmp;
        ospec.sep = "\"";
        ospec.fmt = tmp;
        return 0;
      }
    }
    break;

  default:
    assert(0);
  }

  return -1;
}


static void
output_regf_uninit(int outfd, const struct stat *sbuf)
{
  assert(0 != opathn);

  if (-1 == fchown(outfd, sbuf->st_uid, sbuf->st_gid)) {
    /* File stays with euid:egid, and at most 0600. */
    warnx(errno, "fchown(\"%s\")", opathn);
  }
  else {
    if (sbuf->st_mode & (S_ISUID | S_ISGID | S_ISVTX)) {
      warn("\"%s\": won't restore any of setuid, setgid, sticky",
           opathn);
    }

    if (-1 == fchmod(outfd, sbuf->st_mode & (S_IRWXU | S_IRWXG | S_IRWXO))) {
      /* File stays with orig-uid:orig-gid, and at most 0600. */
      warnx(errno, "fchmod(\"%s\")", opathn);
    }
  }

  {
    struct timespec ts[2];

    ts[0] = get_stat_atime(sbuf);
    ts[1] = get_stat_mtime(sbuf);

    if (-1 == fdutimens(outfd, opathn, ts)) {
      warnx(errno, "fdutimens(\"%s\")", opathn);
    }
  }

  if (-1 == close(outfd)) {
    failx(errno, "close(\"%s\")", opathn);
  }

  free(opathn);
  opathn = 0;
}


int
main(int argc, char **argv)
{
  struct arg *operands;
  static char stderr_buf[BUFSIZ];

  pname = strrchr(argv[0], '/');
  pname = pname ? pname + 1 : argv[0];
  setbuf(stderr, stderr_buf);
  setup_signals();
  opts_setup(&operands, argc, argv);

  /* TODO: For now --small is ignored as it wasn't tested enough...
     Test it, enable, and document (in manpage and usage string)
  */
  small = 0;

  do {
    /* Process operand. */
    {
      int ret;
      struct stat instat;

      ret = input_init(operands, &instat);
      if (-1 != ret) {
        cli();
        if (-1 != output_init(operands, &instat)) {
          work();

          if (OM_REGF == outmode) {
            output_regf_uninit(ospec.fd, &instat);
            if (!keep) {
              input_oprnd_rm(operands);
            }
          }

          /* Display data compression ratio and space savings, but only if the
             user desires so. */
          if (verbose && 0u < ispec.total && 0u < ospec.total) {
            uintmax_t plain_size, compr_size;
            double ratio, savings, ratio_magnitude;

            /* Do the math. Note that converting from uintmax_t to double *may*
              result in precision loss, but that shouldn't matter. */
            plain_size = !decompress ? ispec.total : ospec.total;
            compr_size = ispec.total ^ ospec.total ^ plain_size;
            ratio = (double)compr_size / plain_size;
            savings = 1 - ratio;
            ratio_magnitude = ratio < 1 ? 1 / ratio : ratio;

            infof(&ispec,
                  "compression ratio is %s%.3f%s, space savings is "
                  "%.2f%%", ratio < 1 ? "1:" : "", ratio_magnitude,
                  ratio < 1 ? "" : ":1", 100 * savings);
          }
        }                       /* output available or discarding */
        sti();
        input_uninit();
      }                         /* input available */
    }

    /* Move to next operand. */
    if (0 != operands) {
      struct arg *next;

      next = operands->next;
      free(operands);
      operands = next;
    }
  }
  while (0 != operands);

  assert(0 == opathn);
  if (OM_STDOUT == outmode && -1 == close(STDOUT_FILENO)) {
    failx(errno, "close(stdout)");
  }

  _exit(warned ? EX_WARN : EX_OK);
}
