
/*-----------------------------------------------------------*/
/*--- A block-sorting, lossless compressor        bzip2.c ---*/
/*-----------------------------------------------------------*/

/*--
  This program is bzip2, a lossless, block-sorting data compressor,
  version 0.1pl2, dated 29-Aug-1997.

  Copyright (C) 1996, 1997 by Julian Seward.
     Guildford, Surrey, UK
     email: jseward@acm.org

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

  The GNU General Public License is contained in the file LICENSE.

  This program is based on (at least) the work of:
     Mike Burrows
     David Wheeler
     Peter Fenwick
     Alistair Moffat
     Radford Neal
     Ian H. Witten
     Robert Sedgewick
     Jon L. Bentley

  For more information on these sources, see the file ALGORITHMS.
--*/

/*----------------------------------------------------*/
/*--- IMPORTANT                                    ---*/
/*----------------------------------------------------*/

/*--
   WARNING:
      This program (attempts to) compress data by performing several
      non-trivial transformations on it.  Unless you are 100% familiar
      with *all* the algorithms contained herein, and with the
      consequences of modifying them, you should NOT meddle with the
      compression or decompression machinery.  Incorrect changes can
      and very likely *will* lead to disasterous loss of data.

   DISCLAIMER:
      I TAKE NO RESPONSIBILITY FOR ANY LOSS OF DATA ARISING FROM THE
      USE OF THIS PROGRAM, HOWSOEVER CAUSED.

      Every compression of a file implies an assumption that the
      compressed file can be decompressed to reproduce the original.
      Great efforts in design, coding and testing have been made to
      ensure that this program works correctly.  However, the
      complexity of the algorithms, and, in particular, the presence
      of various special cases in the code which occur with very low
      but non-zero probability make it impossible to rule out the
      possibility of bugs remaining in the program.  DO NOT COMPRESS
      ANY DATA WITH THIS PROGRAM UNLESS YOU ARE PREPARED TO ACCEPT THE
      POSSIBILITY, HOWEVER SMALL, THAT THE DATA WILL NOT BE RECOVERABLE.

      That is not to say this program is inherently unreliable.
      Indeed, I very much hope the opposite is true.  bzip2 has been
      carefully constructed and extensively tested.

   PATENTS:
      To the best of my knowledge, bzip2 does not use any patented
      algorithms.  However, I do not have the resources available to
      carry out a full patent search.  Therefore I cannot give any
      guarantee of the above statement.
--*/



/*----------------------------------------------------*/
/*--- and now for something much more pleasant :-) ---*/
/*----------------------------------------------------*/

/*---------------------------------------------*/
/*--
  Place a 1 beside your platform, and 0 elsewhere.
--*/

/*--
  Generic 32-bit Unix.
  Also works on 64-bit Unix boxes.
--*/
#define BZ_UNIX      1

/*--
  Win32, as seen by Jacob Navia's excellent
  port of (Chris Fraser & David Hanson)'s excellent
  lcc compiler.
--*/
#define BZ_LCCWIN32  0



/*---------------------------------------------*/
/*--
  Some stuff for all platforms.
--*/

#include <stdio.h>
#include <stdlib.h>
#if DEBUG
  #include <assert.h>
#endif
#include <string.h>
#include <signal.h>
#include <math.h>

#define ERROR_IF_EOF(i)       { if ((i) == EOF)  ioError(); }
#define ERROR_IF_NOT_ZERO(i)  { if ((i) != 0)    ioError(); }
#define ERROR_IF_MINUS_ONE(i) { if ((i) == (-1)) ioError(); }


/*---------------------------------------------*/
/*--
   Platform-specific stuff.
--*/

#if BZ_UNIX
   #include <sys/types.h>
   #include <utime.h>
   #include <unistd.h>
   #include <malloc.h>
   #include <sys/stat.h>
   #include <sys/times.h>

   #define Int32   int
   #define UInt32  unsigned int
   #define Char    char
   #define UChar   unsigned char
   #define Int16   short
   #define UInt16  unsigned short

   #define PATH_SEP    '/'
   #define MY_LSTAT    lstat
   #define MY_S_IFREG  S_ISREG
   #define MY_STAT     stat

   #define APPEND_FILESPEC(root, name) \
      root=snocString((root), (name))

   #define SET_BINARY_MODE(fd) /**/

   /*--
      You should try very hard to persuade your C compiler
      to inline the bits marked INLINE.  Otherwise bzip2 will
      run rather slowly.  gcc version 2.x is recommended.
   --*/
   #ifdef __GNUC__
      #define INLINE   inline
      #define NORETURN __attribute__ ((noreturn))
   #else
      #define INLINE   /**/
      #define NORETURN /**/
   #endif
#endif



#if BZ_LCCWIN32
   #include <io.h>
   #include <fcntl.h>
   #include <sys\stat.h>

   #define Int32   int
   #define UInt32  unsigned int
   #define Int16   short
   #define UInt16  unsigned short
   #define Char    char
   #define UChar   unsigned char

   #define INLINE         /**/
   #define NORETURN       /**/
   #define PATH_SEP       '\\'
   #define MY_LSTAT       _stat
   #define MY_STAT        _stat
   #define MY_S_IFREG(x)  ((x) & _S_IFREG)

   #if 0
   /*-- lcc-win32 seems to expand wildcards itself --*/
   #define APPEND_FILESPEC(root, spec)                \
      do {                                            \
         if ((spec)[0] == '-') {                      \
            root = snocString((root), (spec));        \
         } else {                                     \
            struct _finddata_t c_file;                \
            long hFile;                               \
            hFile = _findfirst((spec), &c_file);      \
            if ( hFile == -1L ) {                     \
               root = snocString ((root), (spec));    \
            } else {                                  \
               int anInt = 0;                         \
               while ( anInt == 0 ) {                 \
                  root = snocString((root),           \
                            &c_file.name[0]);         \
                  anInt = _findnext(hFile, &c_file);  \
               }                                      \
            }                                         \
         }                                            \
      } while ( 0 )
   #else
   #define APPEND_FILESPEC(root, name)                \
      root = snocString ((root), (name))
   #endif

   #define SET_BINARY_MODE(fd)                        \
      do {                                            \
         int retVal = setmode ( fileno ( fd ),        \
                               O_BINARY );            \
         ERROR_IF_MINUS_ONE ( retVal );               \
      } while ( 0 )

#endif


/*---------------------------------------------*/
/*--
  Some more stuff for all platforms :-)
--*/

#define Bool   unsigned char
#define True   1
#define False  0

/*--
  IntNative is your platform's `native' int size.
  Only here to avoid probs with 64-bit platforms.
--*/
#define IntNative int


/*--
   change to 1, or compile with -DDEBUG=1 to debug
--*/
#ifndef DEBUG
#define DEBUG 0
#endif


/*---------------------------------------------------*/
/*---                                             ---*/
/*---------------------------------------------------*/

/*--
   Implementation notes, July 1997
   ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

   Memory allocation
   ~~~~~~~~~~~~~~~~~
   All large data structures are allocated on the C heap,
   for better or for worse.  That includes the various
   arrays of pointers, striped words, bytes, frequency
   tables and buffers for compression and decompression.

   bzip2 can operate at various block-sizes, ranging from
   100k to 900k in 100k steps, and it allocates only as
   much as it needs to.  When compressing, we know from the
   command-line options what the block-size is going to be,
   so all allocation can be done at start-up; if that
   succeeds, there can be no further allocation problems.

   Decompression is more complicated.  Each compressed file
   contains, in its header, a byte indicating the block
   size used for compression.  This means bzip2 potentially
   needs to reallocate memory for each file it deals with,
   which in turn opens the possibility for a memory allocation
   failure part way through a run of files, by encountering
   a file requiring a much larger block size than all the
   ones preceding it.

   The policy is to simply give up if a memory allocation
   failure occurs.  During decompression, it would be
   possible to move on to subsequent files in the hope that
   some might ask for a smaller block size, but the
   complications for doing this seem more trouble than they
   are worth.


   Compressed file formats
   ~~~~~~~~~~~~~~~~~~~~~~~
   [This is now entirely different from both 0.21, and from
    any previous Huffman-coded variant of bzip.
    See the associated file bzip2.txt for details.]


   Error conditions
   ~~~~~~~~~~~~~~~~
   Dealing with error conditions is the least satisfactory
   aspect of bzip2.  The policy is to try and leave the
   filesystem in a consistent state, then quit, even if it
   means not processing some of the files mentioned in the
   command line.  `A consistent state' means that a file
   exists either in its compressed or uncompressed form,
   but not both.  This boils down to the rule `delete the
   output file if an error condition occurs, leaving the
   input intact'.  Input files are only deleted when we can
   be pretty sure the output file has been written and
   closed successfully.

   Errors are a dog because there's so many things to
   deal with.  The following can happen mid-file, and
   require cleaning up.

     internal `panics' -- indicating a bug
     corrupted or inconsistent compressed file
     can't allocate enough memory to decompress this file
     I/O error reading/writing/opening/closing
     signal catches -- Control-C, SIGTERM, SIGHUP.

   Other conditions, primarily pertaining to file names,
   can be checked in-between files, which makes dealing
   with them easier.
--*/



/*---------------------------------------------------*/
/*--- Misc (file handling) data decls             ---*/
/*---------------------------------------------------*/

UInt32  bytesIn, bytesOut;
Int32   verbosity;
Bool    keepInputFiles, smallMode, testFailsExist;
UInt32  globalCrc;
Int32   numFileNames, numFilesProcessed;


/*-- source modes; F==file, I==stdin, O==stdout --*/
#define SM_I2O           1
#define SM_F2O           2
#define SM_F2F           3

/*-- operation modes --*/
#define OM_Z             1
#define OM_UNZ           2
#define OM_TEST          3

Int32   opMode;
Int32   srcMode;


Int32   longestFileName;
Char    inName[1024];
Char    outName[1024];
Char    *progName;
Char    progNameReally[1024];
FILE    *outputHandleJustInCase;

void    panic                 ( Char* )          NORETURN;
void    ioError               ( void )           NORETURN;
void    compressOutOfMemory   ( Int32, Int32 )   NORETURN;
void    uncompressOutOfMemory ( Int32, Int32 )   NORETURN;
void    blockOverrun          ( void )           NORETURN;
void    badBlockHeader        ( void )           NORETURN;
void    badBGLengths          ( void )           NORETURN;
void    crcError              ( UInt32, UInt32 ) NORETURN;
void    bitStreamEOF          ( void )           NORETURN;
void    cleanUpAndFail        ( Int32 )          NORETURN;
void    compressedStreamEOF   ( void )           NORETURN;

void*   myMalloc ( Int32 );



/*---------------------------------------------------*/
/*--- Data decls for the front end                ---*/
/*---------------------------------------------------*/

/*--
   The overshoot bytes allow us to avoid most of
   the cost of pointer renormalisation during
   comparison of rotations in sorting.
   The figure of 20 is derived as follows:
      qSort3 allows an overshoot of up to 10.
      It then calls simpleSort, which calls
      fullGtU, also with max overshoot 10.
      fullGtU does up to 10 comparisons without
      renormalising, giving 10+10 == 20.
--*/
#define NUM_OVERSHOOT_BYTES 20

/*--
  These are the main data structures for
  the Burrows-Wheeler transform.
--*/

/*--
  Pointers to compression and decompression
  structures.  Set by
     allocateCompressStructures   and
     setDecompressStructureSizes

  The structures are always set to be suitable
  for a block of size 100000 * blockSize100k.
--*/
UChar    *block;    /*-- compress   --*/
UInt16   *quadrant; /*-- compress   --*/
Int32    *zptr;     /*-- compress   --*/ 
UInt16   *szptr;    /*-- overlays zptr ---*/
Int32    *ftab;     /*-- compress   --*/

UInt16   *ll16;     /*-- small decompress --*/
UChar    *ll4;      /*-- small decompress --*/

Int32    *tt;       /*-- fast decompress  --*/
UChar    *ll8;      /*-- fast decompress  --*/


/*--
  freq table collected to save a pass over the data
  during decompression.
--*/
Int32   unzftab[256];


/*--
   index of the last char in the block, so
   the block size == last + 1.
--*/
Int32  last;


/*--
  index in zptr[] of original string after sorting.
--*/
Int32  origPtr;


/*--
  always: in the range 0 .. 9.
  The current block size is 100000 * this number.
--*/
Int32  blockSize100k;


/*--
  Used when sorting.  If too many long comparisons
  happen, we stop sorting, randomise the block 
  slightly, and try again.
--*/

Int32  workFactor;
Int32  workDone;
Int32  workLimit;
Bool   blockRandomised;
Bool   firstAttempt;
Int32  nBlocksRandomised;



/*---------------------------------------------------*/
/*--- Data decls for the back end                 ---*/
/*---------------------------------------------------*/

#define MAX_ALPHA_SIZE 258
#define MAX_CODE_LEN    23

#define RUNA 0
#define RUNB 1

#define N_GROUPS 6
#define G_SIZE   50
#define N_ITERS  4

#define MAX_SELECTORS (2 + (900000 / G_SIZE))

Bool  inUse[256];
Int32 nInUse;

UChar seqToUnseq[256];
UChar unseqToSeq[256];

UChar selector   [MAX_SELECTORS];
UChar selectorMtf[MAX_SELECTORS];

Int32 nMTF;

Int32 mtfFreq[MAX_ALPHA_SIZE];

UChar len  [N_GROUPS][MAX_ALPHA_SIZE];

/*-- decompress only --*/
Int32 limit  [N_GROUPS][MAX_ALPHA_SIZE];
Int32 base   [N_GROUPS][MAX_ALPHA_SIZE];
Int32 perm   [N_GROUPS][MAX_ALPHA_SIZE];
Int32 minLens[N_GROUPS];

/*-- compress only --*/
Int32  code [N_GROUPS][MAX_ALPHA_SIZE];
Int32  rfreq[N_GROUPS][MAX_ALPHA_SIZE];


/*---------------------------------------------------*/
/*--- 32-bit CRC grunge                           ---*/
/*---------------------------------------------------*/

/*--
  I think this is an implementation of the AUTODIN-II,
  Ethernet & FDDI 32-bit CRC standard.  Vaguely derived
  from code by Rob Warnock, in Section 51 of the
  comp.compression FAQ.
--*/

UInt32 crc32Table[256] = {

   /*-- Ugly, innit? --*/

   0x00000000UL, 0x04c11db7UL, 0x09823b6eUL, 0x0d4326d9UL,
   0x130476dcUL, 0x17c56b6bUL, 0x1a864db2UL, 0x1e475005UL,
   0x2608edb8UL, 0x22c9f00fUL, 0x2f8ad6d6UL, 0x2b4bcb61UL,
   0x350c9b64UL, 0x31cd86d3UL, 0x3c8ea00aUL, 0x384fbdbdUL,
   0x4c11db70UL, 0x48d0c6c7UL, 0x4593e01eUL, 0x4152fda9UL,
   0x5f15adacUL, 0x5bd4b01bUL, 0x569796c2UL, 0x52568b75UL,
   0x6a1936c8UL, 0x6ed82b7fUL, 0x639b0da6UL, 0x675a1011UL,
   0x791d4014UL, 0x7ddc5da3UL, 0x709f7b7aUL, 0x745e66cdUL,
   0x9823b6e0UL, 0x9ce2ab57UL, 0x91a18d8eUL, 0x95609039UL,
   0x8b27c03cUL, 0x8fe6dd8bUL, 0x82a5fb52UL, 0x8664e6e5UL,
   0xbe2b5b58UL, 0xbaea46efUL, 0xb7a96036UL, 0xb3687d81UL,
   0xad2f2d84UL, 0xa9ee3033UL, 0xa4ad16eaUL, 0xa06c0b5dUL,
   0xd4326d90UL, 0xd0f37027UL, 0xddb056feUL, 0xd9714b49UL,
   0xc7361b4cUL, 0xc3f706fbUL, 0xceb42022UL, 0xca753d95UL,
   0xf23a8028UL, 0xf6fb9d9fUL, 0xfbb8bb46UL, 0xff79a6f1UL,
   0xe13ef6f4UL, 0xe5ffeb43UL, 0xe8bccd9aUL, 0xec7dd02dUL,
   0x34867077UL, 0x30476dc0UL, 0x3d044b19UL, 0x39c556aeUL,
   0x278206abUL, 0x23431b1cUL, 0x2e003dc5UL, 0x2ac12072UL,
   0x128e9dcfUL, 0x164f8078UL, 0x1b0ca6a1UL, 0x1fcdbb16UL,
   0x018aeb13UL, 0x054bf6a4UL, 0x0808d07dUL, 0x0cc9cdcaUL,
   0x7897ab07UL, 0x7c56b6b0UL, 0x71159069UL, 0x75d48ddeUL,
   0x6b93dddbUL, 0x6f52c06cUL, 0x6211e6b5UL, 0x66d0fb02UL,
   0x5e9f46bfUL, 0x5a5e5b08UL, 0x571d7dd1UL, 0x53dc6066UL,
   0x4d9b3063UL, 0x495a2dd4UL, 0x44190b0dUL, 0x40d816baUL,
   0xaca5c697UL, 0xa864db20UL, 0xa527fdf9UL, 0xa1e6e04eUL,
   0xbfa1b04bUL, 0xbb60adfcUL, 0xb6238b25UL, 0xb2e29692UL,
   0x8aad2b2fUL, 0x8e6c3698UL, 0x832f1041UL, 0x87ee0df6UL,
   0x99a95df3UL, 0x9d684044UL, 0x902b669dUL, 0x94ea7b2aUL,
   0xe0b41de7UL, 0xe4750050UL, 0xe9362689UL, 0xedf73b3eUL,
   0xf3b06b3bUL, 0xf771768cUL, 0xfa325055UL, 0xfef34de2UL,
   0xc6bcf05fUL, 0xc27dede8UL, 0xcf3ecb31UL, 0xcbffd686UL,
   0xd5b88683UL, 0xd1799b34UL, 0xdc3abdedUL, 0xd8fba05aUL,
   0x690ce0eeUL, 0x6dcdfd59UL, 0x608edb80UL, 0x644fc637UL,
   0x7a089632UL, 0x7ec98b85UL, 0x738aad5cUL, 0x774bb0ebUL,
   0x4f040d56UL, 0x4bc510e1UL, 0x46863638UL, 0x42472b8fUL,
   0x5c007b8aUL, 0x58c1663dUL, 0x558240e4UL, 0x51435d53UL,
   0x251d3b9eUL, 0x21dc2629UL, 0x2c9f00f0UL, 0x285e1d47UL,
   0x36194d42UL, 0x32d850f5UL, 0x3f9b762cUL, 0x3b5a6b9bUL,
   0x0315d626UL, 0x07d4cb91UL, 0x0a97ed48UL, 0x0e56f0ffUL,
   0x1011a0faUL, 0x14d0bd4dUL, 0x19939b94UL, 0x1d528623UL,
   0xf12f560eUL, 0xf5ee4bb9UL, 0xf8ad6d60UL, 0xfc6c70d7UL,
   0xe22b20d2UL, 0xe6ea3d65UL, 0xeba91bbcUL, 0xef68060bUL,
   0xd727bbb6UL, 0xd3e6a601UL, 0xdea580d8UL, 0xda649d6fUL,
   0xc423cd6aUL, 0xc0e2d0ddUL, 0xcda1f604UL, 0xc960ebb3UL,
   0xbd3e8d7eUL, 0xb9ff90c9UL, 0xb4bcb610UL, 0xb07daba7UL,
   0xae3afba2UL, 0xaafbe615UL, 0xa7b8c0ccUL, 0xa379dd7bUL,
   0x9b3660c6UL, 0x9ff77d71UL, 0x92b45ba8UL, 0x9675461fUL,
   0x8832161aUL, 0x8cf30badUL, 0x81b02d74UL, 0x857130c3UL,
   0x5d8a9099UL, 0x594b8d2eUL, 0x5408abf7UL, 0x50c9b640UL,
   0x4e8ee645UL, 0x4a4ffbf2UL, 0x470cdd2bUL, 0x43cdc09cUL,
   0x7b827d21UL, 0x7f436096UL, 0x7200464fUL, 0x76c15bf8UL,
   0x68860bfdUL, 0x6c47164aUL, 0x61043093UL, 0x65c52d24UL,
   0x119b4be9UL, 0x155a565eUL, 0x18197087UL, 0x1cd86d30UL,
   0x029f3d35UL, 0x065e2082UL, 0x0b1d065bUL, 0x0fdc1becUL,
   0x3793a651UL, 0x3352bbe6UL, 0x3e119d3fUL, 0x3ad08088UL,
   0x2497d08dUL, 0x2056cd3aUL, 0x2d15ebe3UL, 0x29d4f654UL,
   0xc5a92679UL, 0xc1683bceUL, 0xcc2b1d17UL, 0xc8ea00a0UL,
   0xd6ad50a5UL, 0xd26c4d12UL, 0xdf2f6bcbUL, 0xdbee767cUL,
   0xe3a1cbc1UL, 0xe760d676UL, 0xea23f0afUL, 0xeee2ed18UL,
   0xf0a5bd1dUL, 0xf464a0aaUL, 0xf9278673UL, 0xfde69bc4UL,
   0x89b8fd09UL, 0x8d79e0beUL, 0x803ac667UL, 0x84fbdbd0UL,
   0x9abc8bd5UL, 0x9e7d9662UL, 0x933eb0bbUL, 0x97ffad0cUL,
   0xafb010b1UL, 0xab710d06UL, 0xa6322bdfUL, 0xa2f33668UL,
   0xbcb4666dUL, 0xb8757bdaUL, 0xb5365d03UL, 0xb1f740b4UL
};


/*---------------------------------------------*/
void initialiseCRC ( void )
{
   globalCrc = 0xffffffffUL;
}


/*---------------------------------------------*/
UInt32 getFinalCRC ( void )
{
   return ~globalCrc;
}


/*---------------------------------------------*/
UInt32 getGlobalCRC ( void )
{
   return globalCrc;
}


/*---------------------------------------------*/
void setGlobalCRC ( UInt32 newCrc )
{
   globalCrc = newCrc;
}


/*---------------------------------------------*/
#define UPDATE_CRC(crcVar,cha)              \
{                                           \
   crcVar = (crcVar << 8) ^                 \
            crc32Table[(crcVar >> 24) ^     \
                       ((UChar)cha)];       \
}


/*---------------------------------------------------*/
/*--- Bit stream I/O                              ---*/
/*---------------------------------------------------*/


UInt32 bsBuff;
Int32  bsLive;
FILE*  bsStream;
Bool   bsWriting;


/*---------------------------------------------*/
void bsSetStream ( FILE* f, Bool wr )
{
   if (bsStream != NULL) panic ( "bsSetStream" );
   bsStream = f;
   bsLive = 0;
   bsBuff = 0;
   bytesOut = 0;
   bytesIn = 0;
   bsWriting = wr;
}


/*---------------------------------------------*/
void bsFinishedWithStream ( void )
{
   if (bsWriting)
      while (bsLive > 0) {
         fputc ( (UChar)(bsBuff >> 24), bsStream );
         bsBuff <<= 8;
         bsLive -= 8;
         bytesOut++;
      }
   bsStream = NULL;
}


/*---------------------------------------------*/
#define bsNEEDR(nz)                           \
{                                             \
   while (bsLive < nz) {                      \
      Int32 zzi = fgetc ( bsStream );         \
      if (zzi == EOF) compressedStreamEOF();  \
      bsBuff = (bsBuff << 8) | (zzi & 0xffL); \
      bsLive += 8;                            \
   }                                          \
}


/*---------------------------------------------*/
#define bsNEEDW(nz)                           \
{                                             \
   while (bsLive >= 8) {                      \
      fputc ( (UChar)(bsBuff >> 24),          \
               bsStream );                    \
      bsBuff <<= 8;                           \
      bsLive -= 8;                            \
      bytesOut++;                             \
   }                                          \
}


/*---------------------------------------------*/
#define bsR1(vz)                              \
{                                             \
   bsNEEDR(1);                                \
   vz = (bsBuff >> (bsLive-1)) & 1;           \
   bsLive--;                                  \
}


/*---------------------------------------------*/
INLINE UInt32 bsR ( Int32 n )
{
   UInt32 v;
   bsNEEDR ( n );
   v = (bsBuff >> (bsLive-n)) & ((1 << n)-1);
   bsLive -= n;
   return v;
}


/*---------------------------------------------*/
INLINE void bsW ( Int32 n, UInt32 v )
{
   bsNEEDW ( n );
   bsBuff |= (v << (32 - bsLive - n));
   bsLive += n;
}


/*---------------------------------------------*/
UChar bsGetUChar ( void )
{
   return (UChar)bsR(8);
}


/*---------------------------------------------*/
void bsPutUChar ( UChar c )
{
   bsW(8, (UInt32)c );
}


/*---------------------------------------------*/
Int32 bsGetUInt32 ( void )
{
   UInt32 u;
   u = 0;
   u = (u << 8) | bsR(8);
   u = (u << 8) | bsR(8);
   u = (u << 8) | bsR(8);
   u = (u << 8) | bsR(8);
   return u;
}


/*---------------------------------------------*/
UInt32 bsGetIntVS ( UInt32 numBits )
{
   return (UInt32)bsR(numBits);
}


/*---------------------------------------------*/
UInt32 bsGetInt32 ( void )
{
   return (Int32)bsGetUInt32();
}


/*---------------------------------------------*/
void bsPutUInt32 ( UInt32 u )
{
   bsW ( 8, (u >> 24) & 0xffL );
   bsW ( 8, (u >> 16) & 0xffL );
   bsW ( 8, (u >>  8) & 0xffL );
   bsW ( 8,  u        & 0xffL );
}


/*---------------------------------------------*/
void bsPutInt32 ( Int32 c )
{
   bsPutUInt32 ( (UInt32)c );
}


/*---------------------------------------------*/
void bsPutIntVS ( Int32 numBits, UInt32 c )
{
   bsW ( numBits, c );
}


/*---------------------------------------------------*/
/*--- Huffman coding low-level stuff              ---*/
/*---------------------------------------------------*/

#define WEIGHTOF(zz0)  ((zz0) & 0xffffff00)
#define DEPTHOF(zz1)   ((zz1) & 0x000000ff)
#define MYMAX(zz2,zz3) ((zz2) > (zz3) ? (zz2) : (zz3))

#define ADDWEIGHTS(zw1,zw2)                           \
   (WEIGHTOF(zw1)+WEIGHTOF(zw2)) |                    \
   (1 + MYMAX(DEPTHOF(zw1),DEPTHOF(zw2)))

#define UPHEAP(z)                                     \
{                                                     \
   Int32 zz, tmp;                                     \
   zz = z; tmp = heap[zz];                            \
   while (weight[tmp] < weight[heap[zz >> 1]]) {      \
      heap[zz] = heap[zz >> 1];                       \
      zz >>= 1;                                       \
   }                                                  \
   heap[zz] = tmp;                                    \
}

#define DOWNHEAP(z)                                   \
{                                                     \
   Int32 zz, yy, tmp;                                 \
   zz = z; tmp = heap[zz];                            \
   while (True) {                                     \
      yy = zz << 1;                                   \
      if (yy > nHeap) break;                          \
      if (yy < nHeap &&                               \
          weight[heap[yy+1]] < weight[heap[yy]])      \
         yy++;                                        \
      if (weight[tmp] < weight[heap[yy]]) break;      \
      heap[zz] = heap[yy];                            \
      zz = yy;                                        \
   }                                                  \
   heap[zz] = tmp;                                    \
}


/*---------------------------------------------*/
void hbMakeCodeLengths ( UChar *len, 
                         Int32 *freq,
                         Int32 alphaSize,
                         Int32 maxLen )
{
   /*--
      Nodes and heap entries run from 1.  Entry 0
      for both the heap and nodes is a sentinel.
   --*/
   Int32 nNodes, nHeap, n1, n2, i, j, k;
   Bool  tooLong;

   Int32 heap   [ MAX_ALPHA_SIZE + 2 ];
   Int32 weight [ MAX_ALPHA_SIZE * 2 ];
   Int32 parent [ MAX_ALPHA_SIZE * 2 ]; 

   for (i = 0; i < alphaSize; i++)
      weight[i+1] = (freq[i] == 0 ? 1 : freq[i]) << 8;

   while (True) {

      nNodes = alphaSize;
      nHeap = 0;

      heap[0] = 0;
      weight[0] = 0;
      parent[0] = -2;

      for (i = 1; i <= alphaSize; i++) {
         parent[i] = -1;
         nHeap++;
         heap[nHeap] = i;
         UPHEAP(nHeap);
      }
      if (!(nHeap < (MAX_ALPHA_SIZE+2))) 
         panic ( "hbMakeCodeLengths(1)" );
   
      while (nHeap > 1) {
         n1 = heap[1]; heap[1] = heap[nHeap]; nHeap--; DOWNHEAP(1);
         n2 = heap[1]; heap[1] = heap[nHeap]; nHeap--; DOWNHEAP(1);
         nNodes++;
         parent[n1] = parent[n2] = nNodes;
         weight[nNodes] = ADDWEIGHTS(weight[n1], weight[n2]);
         parent[nNodes] = -1;
         nHeap++;
         heap[nHeap] = nNodes;
         UPHEAP(nHeap);
      }
      if (!(nNodes < (MAX_ALPHA_SIZE * 2)))
         panic ( "hbMakeCodeLengths(2)" );

      tooLong = False;
      for (i = 1; i <= alphaSize; i++) {
         j = 0;
         k = i;
         while (parent[k] >= 0) { k = parent[k]; j++; }
         len[i-1] = j;
         if (j > maxLen) tooLong = True;
      }
      
      if (! tooLong) break;

      for (i = 1; i < alphaSize; i++) {
         j = weight[i] >> 8;
         j = 1 + (j / 2);
         weight[i] = j << 8;
      }
   }
}


/*---------------------------------------------*/
void hbAssignCodes ( Int32 *code,
                     UChar *length,
                     Int32 minLen,
                     Int32 maxLen,
                     Int32 alphaSize )
{
   Int32 n, vec, i;

   vec = 0;
   for (n = minLen; n <= maxLen; n++) {
      for (i = 0; i < alphaSize; i++)
         if (length[i] == n) { code[i] = vec; vec++; };
      vec <<= 1;
   }
}


/*---------------------------------------------*/
void hbCreateDecodeTables ( Int32 *limit,
                            Int32 *base,
                            Int32 *perm,
                            UChar *length,
                            Int32 minLen,
                            Int32 maxLen,
                            Int32 alphaSize )
{
   Int32 pp, i, j, vec;

   pp = 0;
   for (i = minLen; i <= maxLen; i++)
      for (j = 0; j < alphaSize; j++)
         if (length[j] == i) { perm[pp] = j; pp++; };

   for (i = 0; i < MAX_CODE_LEN; i++) base[i] = 0;
   for (i = 0; i < alphaSize; i++) base[length[i]+1]++;

   for (i = 1; i < MAX_CODE_LEN; i++) base[i] += base[i-1];

   for (i = 0; i < MAX_CODE_LEN; i++) limit[i] = 0;
   vec = 0;

   for (i = minLen; i <= maxLen; i++) {
      vec += (base[i+1] - base[i]);
      limit[i] = vec-1;
      vec <<= 1;
   }
   for (i = minLen + 1; i <= maxLen; i++)
      base[i] = ((limit[i-1] + 1) << 1) - base[i];
}



/*---------------------------------------------------*/
/*--- Undoing the reversible transformation       ---*/
/*---------------------------------------------------*/

/*---------------------------------------------*/
#define SET_LL4(i,n)                                          \
   { if (((i) & 0x1) == 0)                                    \
        ll4[(i) >> 1] = (ll4[(i) >> 1] & 0xf0) | (n); else    \
        ll4[(i) >> 1] = (ll4[(i) >> 1] & 0x0f) | ((n) << 4);  \
   }

#define GET_LL4(i)                             \
    (((UInt32)(ll4[(i) >> 1])) >> (((i) << 2) & 0x4) & 0xF)

#define SET_LL(i,n)                       \
   { ll16[i] = (UInt16)(n & 0x0000ffff);  \
     SET_LL4(i, n >> 16);                 \
   }

#define GET_LL(i) \
   (((UInt32)ll16[i]) | (GET_LL4(i) << 16))


/*---------------------------------------------*/
/*--
  Manage memory for compression/decompression.
  When compressing, a single block size applies to
  all files processed, and that's set when the
  program starts.  But when decompressing, each file
  processed could have been compressed with a
  different block size, so we may have to free
  and reallocate on a per-file basis.

  A call with argument of zero means
  `free up everything.'  And a value of zero for
  blockSize100k means no memory is currently allocated.
--*/


/*---------------------------------------------*/
void allocateCompressStructures ( void )
{
   Int32 n  = 100000 * blockSize100k;
   block    = malloc ( (n + 1 + NUM_OVERSHOOT_BYTES) * sizeof(UChar) );
   quadrant = malloc ( (n     + NUM_OVERSHOOT_BYTES) * sizeof(Int16) );
   zptr     = malloc ( n                             * sizeof(Int32) );
   ftab     = malloc ( 65537                         * sizeof(Int32) );

   if (block == NULL || quadrant == NULL ||
       zptr == NULL  || ftab == NULL) {
      Int32 totalDraw
         = (n + 1 + NUM_OVERSHOOT_BYTES) * sizeof(UChar) +
           (n     + NUM_OVERSHOOT_BYTES) * sizeof(Int16) +
           n                             * sizeof(Int32) +
           65537                         * sizeof(Int32);

      compressOutOfMemory ( totalDraw, n );
   }

   /*--
      Since we want valid indexes for block of
      -1 to n + NUM_OVERSHOOT_BYTES - 1
      inclusive.
   --*/
   block++;

   /*--
      The back end needs a place to store the MTF values
      whilst it calculates the coding tables.  We could
      put them in the zptr array.  However, these values
      will fit in a short, so we overlay szptr at the 
      start of zptr, in the hope of reducing the number
      of cache misses induced by the multiple traversals
      of the MTF values when calculating coding tables.
      Seems to improve compression speed by about 1%.
   --*/
   szptr = (UInt16*)zptr;
}


/*---------------------------------------------*/
void setDecompressStructureSizes ( Int32 newSize100k )
{
   if (! (0 <= newSize100k   && newSize100k   <= 9 &&
          0 <= blockSize100k && blockSize100k <= 9))
      panic ( "setDecompressStructureSizes" );

   if (newSize100k == blockSize100k) return;

   blockSize100k = newSize100k;

   if (ll16  != NULL) free ( ll16  );
   if (ll4   != NULL) free ( ll4   );
   if (ll8   != NULL) free ( ll8   );
   if (tt    != NULL) free ( tt    );

   if (newSize100k == 0) return;

   if (smallMode) {

      Int32 n = 100000 * newSize100k;
      ll16    = malloc ( n * sizeof(UInt16) );
      ll4     = malloc ( ((n+1) >> 1) * sizeof(UChar) );

      if (ll4 == NULL || ll16 == NULL) {
         Int32 totalDraw
            = n * sizeof(Int16) + ((n+1) >> 1) * sizeof(UChar);
         uncompressOutOfMemory ( totalDraw, n );
      }

   } else {

      Int32 n = 100000 * newSize100k;
      ll8     = malloc ( n * sizeof(UChar) );
      tt      = malloc ( n * sizeof(Int32) );

      if (ll8 == NULL || tt == NULL) {
         Int32 totalDraw
            = n * sizeof(UChar) + n * sizeof(UInt32);
         uncompressOutOfMemory ( totalDraw, n );
      }

   }
}



/*---------------------------------------------------*/
/*--- The new back end                            ---*/
/*---------------------------------------------------*/

/*---------------------------------------------*/
void makeMaps ( void )
{
   Int32 i;
   nInUse = 0;
   for (i = 0; i < 256; i++)
      if (inUse[i]) {
         seqToUnseq[nInUse] = i;
         unseqToSeq[i] = nInUse;
         nInUse++;
      }
}


/*---------------------------------------------*/
void generateMTFValues ( void )
{
   UChar  yy[256];
   Int32  i, j;
   UChar  tmp;
   UChar  tmp2;
   Int32  zPend;
   Int32  wr;
   Int32  EOB;

   makeMaps();
   EOB = nInUse+1;

   for (i = 0; i <= EOB; i++) mtfFreq[i] = 0;

   wr = 0;
   zPend = 0;
   for (i = 0; i < nInUse; i++) yy[i] = (UChar) i;
   

   for (i = 0; i <= last; i++) {
      UChar ll_i;

      #if DEBUG
         assert (wr <= i);
      #endif

      ll_i = unseqToSeq[block[zptr[i] - 1]];
      #if DEBUG
         assert (ll_i < nInUse);
      #endif

      j = 0;
      tmp = yy[j];
      while ( ll_i != tmp ) {
         j++;
         tmp2 = tmp;
         tmp = yy[j];
         yy[j] = tmp2;
      };
      yy[0] = tmp;

      if (j == 0) {
         zPend++;
      } else {
         if (zPend > 0) {
            zPend--;
            while (True) {
               switch (zPend % 2) {
                  case 0: szptr[wr] = RUNA; wr++; mtfFreq[RUNA]++; break;
                  case 1: szptr[wr] = RUNB; wr++; mtfFreq[RUNB]++; break;
               };
               if (zPend < 2) break;
               zPend = (zPend - 2) / 2;
            };
            zPend = 0;
         }
         szptr[wr] = j+1; wr++; mtfFreq[j+1]++;
      }
   }

   if (zPend > 0) {
      zPend--;
      while (True) {
         switch (zPend % 2) {
            case 0:  szptr[wr] = RUNA; wr++; mtfFreq[RUNA]++; break;
            case 1:  szptr[wr] = RUNB; wr++; mtfFreq[RUNB]++; break;
         };
         if (zPend < 2) break;
         zPend = (zPend - 2) / 2;
      };
   }

   szptr[wr] = EOB; wr++; mtfFreq[EOB]++;

   nMTF = wr;
}


/*---------------------------------------------*/
#define LESSER_ICOST  0
#define GREATER_ICOST 15

void sendMTFValues ( void )
{
   Int32 v, t, i, j, gs, ge, totc, bt, bc, iter;
   Int32 nSelectors, alphaSize, minLen, maxLen, selCtr;
   Int32 nGroups, nBytes;

   /*--
   UChar  len [N_GROUPS][MAX_ALPHA_SIZE];
   is a global since the decoder also needs it.

   Int32  code[N_GROUPS][MAX_ALPHA_SIZE];
   Int32  rfreq[N_GROUPS][MAX_ALPHA_SIZE];
   are also globals only used in this proc.
   Made global to keep stack frame size small.
   --*/


   UInt16 cost[N_GROUPS];
   Int32  fave[N_GROUPS];

   if (verbosity >= 3)
      fprintf ( stderr, 
                "      %d in block, %d after MTF & 1-2 coding, %d+2 syms in use\n", 
                last+1, nMTF, nInUse );

   alphaSize = nInUse+2;
   for (t = 0; t < N_GROUPS; t++)
      for (v = 0; v < alphaSize; v++)
         len[t][v] = GREATER_ICOST;

   /*--- Decide how many coding tables to use ---*/
   if (nMTF <= 0) panic ( "sendMTFValues(0)" );
   if (nMTF < 200) nGroups = 2; else
   if (nMTF < 800) nGroups = 4; else
                   nGroups = 6;

   /*--- Generate an initial set of coding tables ---*/
   { 
      Int32 nPart, remF, tFreq, aFreq;

      nPart = nGroups;
      remF  = nMTF;
      gs = 0;
      while (nPart > 0) {
         tFreq = remF / nPart;
         ge = gs-1;
         aFreq = 0;
         while (aFreq < tFreq && ge < alphaSize-1) {
            ge++;
            aFreq += mtfFreq[ge];
         }

         if (ge > gs 
             && nPart != nGroups && nPart != 1 
             && ((nGroups-nPart) % 2 == 1)) {
            aFreq -= mtfFreq[ge];
            ge--;
         }

         if (verbosity >= 3)
            fprintf ( stderr, 
                      "      initial group %d, [%d .. %d], has %d syms (%4.1f%%)\n",
                              nPart, gs, ge, aFreq, 
                              (100.0 * (float)aFreq) / (float)nMTF );
 
         for (v = 0; v < alphaSize; v++)
            if (v >= gs && v <= ge) 
               len[nPart-1][v] = LESSER_ICOST; else
               len[nPart-1][v] = GREATER_ICOST;
 
         nPart--;
         gs = ge+1;
         remF -= aFreq;
      }
   }

   /*--- 
      Iterate up to N_ITERS times to improve the tables.
   ---*/
   for (iter = 0; iter < N_ITERS; iter++) {

      for (t = 0; t < nGroups; t++) fave[t] = 0;

      for (t = 0; t < nGroups; t++)
         for (v = 0; v < alphaSize; v++)
            rfreq[t][v] = 0;

      nSelectors = 0;
      totc = 0;
      gs = 0;
      while (True) {

         /*--- Set group start & end marks. --*/
         if (gs >= nMTF) break;
         ge = gs + G_SIZE - 1; 
         if (ge >= nMTF) ge = nMTF-1;

         /*-- 
            Calculate the cost of this group as coded
            by each of the coding tables.
         --*/
         for (t = 0; t < nGroups; t++) cost[t] = 0;

         if (nGroups == 6) {
            register UInt16 cost0, cost1, cost2, cost3, cost4, cost5;
            cost0 = cost1 = cost2 = cost3 = cost4 = cost5 = 0;
            for (i = gs; i <= ge; i++) { 
               UInt16 icv = szptr[i];
               cost0 += len[0][icv];
               cost1 += len[1][icv];
               cost2 += len[2][icv];
               cost3 += len[3][icv];
               cost4 += len[4][icv];
               cost5 += len[5][icv];
            }
            cost[0] = cost0; cost[1] = cost1; cost[2] = cost2;
            cost[3] = cost3; cost[4] = cost4; cost[5] = cost5;
         } else {
            for (i = gs; i <= ge; i++) { 
               UInt16 icv = szptr[i];
               for (t = 0; t < nGroups; t++) cost[t] += len[t][icv];
            }
         }
 
         /*-- 
            Find the coding table which is best for this group,
            and record its identity in the selector table.
         --*/
         bc = 999999999; bt = -1;
         for (t = 0; t < nGroups; t++)
            if (cost[t] < bc) { bc = cost[t]; bt = t; };
         totc += bc;
         fave[bt]++;
         selector[nSelectors] = bt;
         nSelectors++;

         /*-- 
            Increment the symbol frequencies for the selected table.
          --*/
         for (i = gs; i <= ge; i++)
            rfreq[bt][ szptr[i] ]++;

         gs = ge+1;
      }
      if (verbosity >= 3) {
         fprintf ( stderr, 
                   "      pass %d: size is %d, grp uses are ", 
                   iter+1, totc/8 );
         for (t = 0; t < nGroups; t++)
            fprintf ( stderr, "%d ", fave[t] );
         fprintf ( stderr, "\n" );
      }

      /*--
        Recompute the tables based on the accumulated frequencies.
      --*/
      for (t = 0; t < nGroups; t++)
         hbMakeCodeLengths ( &len[t][0], &rfreq[t][0], alphaSize, 20 );
   }


   if (!(nGroups < 8)) panic ( "sendMTFValues(1)" );
   if (!(nSelectors < 32768 &&
         nSelectors <= (2 + (900000 / G_SIZE))))
                       panic ( "sendMTFValues(2)" );


   /*--- Compute MTF values for the selectors. ---*/
   {
      UChar pos[N_GROUPS], ll_i, tmp2, tmp;
      for (i = 0; i < nGroups; i++) pos[i] = i;
      for (i = 0; i < nSelectors; i++) {
         ll_i = selector[i];
         j = 0;
         tmp = pos[j];
         while ( ll_i != tmp ) {
            j++;
            tmp2 = tmp;
            tmp = pos[j];
            pos[j] = tmp2;
         };
         pos[0] = tmp;
         selectorMtf[i] = j;
      }
   };

   /*--- Assign actual codes for the tables. --*/
   for (t = 0; t < nGroups; t++) {
      minLen = 32;
      maxLen = 0;
      for (i = 0; i < alphaSize; i++) {
         if (len[t][i] > maxLen) maxLen = len[t][i];
         if (len[t][i] < minLen) minLen = len[t][i];
      }
      if (maxLen > 20) panic ( "sendMTFValues(3)" );
      if (minLen < 1)  panic ( "sendMTFValues(4)" );
      hbAssignCodes ( &code[t][0], &len[t][0], 
                      minLen, maxLen, alphaSize );
   }

   /*--- Transmit the mapping table. ---*/
   { 
      Bool inUse16[16];
      for (i = 0; i < 16; i++) {
          inUse16[i] = False;
          for (j = 0; j < 16; j++)
             if (inUse[i * 16 + j]) inUse16[i] = True;
      }
     
      nBytes = bytesOut;
      for (i = 0; i < 16; i++)
         if (inUse16[i]) bsW(1,1); else bsW(1,0);

      for (i = 0; i < 16; i++)
         if (inUse16[i])
            for (j = 0; j < 16; j++)
               if (inUse[i * 16 + j]) bsW(1,1); else bsW(1,0);

      if (verbosity >= 3) 
         fprintf ( stderr, "      bytes: mapping %d, ", bytesOut-nBytes );
   }

   /*--- Now the selectors. ---*/
   nBytes = bytesOut;
   bsW ( 3, nGroups );
   bsW ( 15, nSelectors );
   for (i = 0; i < nSelectors; i++) { 
      for (j = 0; j < selectorMtf[i]; j++) bsW(1,1);
      bsW(1,0);
   }
   if (verbosity >= 3)
      fprintf ( stderr, "selectors %d, ", bytesOut-nBytes );

   /*--- Now the coding tables. ---*/
   nBytes = bytesOut;

   for (t = 0; t < nGroups; t++) {
      Int32 curr = len[t][0];
      bsW ( 5, curr );
      for (i = 0; i < alphaSize; i++) {
         while (curr < len[t][i]) { bsW(2,2); curr++; /* 10 */ };
         while (curr > len[t][i]) { bsW(2,3); curr--; /* 11 */ };
         bsW ( 1, 0 );
      }
   }

   if (verbosity >= 3)
      fprintf ( stderr, "code lengths %d, ", bytesOut-nBytes );

   /*--- And finally, the block data proper ---*/
   nBytes = bytesOut;
   selCtr = 0;
   gs = 0;
   while (True) {
      if (gs >= nMTF) break;
      ge = gs + G_SIZE - 1; 
      if (ge >= nMTF) ge = nMTF-1;
      for (i = gs; i <= ge; i++) { 
         #if DEBUG
            assert (selector[selCtr] < nGroups);
         #endif
         bsW ( len  [selector[selCtr]] [szptr[i]],
               code [selector[selCtr]] [szptr[i]] );
      }

      gs = ge+1;
      selCtr++;
   }
   if (!(selCtr == nSelectors)) panic ( "sendMTFValues(5)" );

   if (verbosity >= 3)
      fprintf ( stderr, "codes %d\n", bytesOut-nBytes );
}


/*---------------------------------------------*/
void moveToFrontCodeAndSend ( void )
{
   bsPutIntVS ( 24, origPtr );
   generateMTFValues();
   sendMTFValues();
}


/*---------------------------------------------*/
void recvDecodingTables ( void )
{
   Int32 i, j, t, nGroups, nSelectors, alphaSize;
   Int32 minLen, maxLen;
   Bool inUse16[16];

   /*--- Receive the mapping table ---*/
   for (i = 0; i < 16; i++)
      if (bsR(1) == 1) 
         inUse16[i] = True; else 
         inUse16[i] = False;

   for (i = 0; i < 256; i++) inUse[i] = False;

   for (i = 0; i < 16; i++)
      if (inUse16[i])
         for (j = 0; j < 16; j++)
            if (bsR(1) == 1) inUse[i * 16 + j] = True;

   makeMaps();
   alphaSize = nInUse+2;

   /*--- Now the selectors ---*/
   nGroups = bsR ( 3 );
   nSelectors = bsR ( 15 );
   for (i = 0; i < nSelectors; i++) {
      j = 0;
      while (bsR(1) == 1) j++;
      selectorMtf[i] = j;
   }

   /*--- Undo the MTF values for the selectors. ---*/
   {
      UChar pos[N_GROUPS], tmp, v;
      for (v = 0; v < nGroups; v++) pos[v] = v;
   
      for (i = 0; i < nSelectors; i++) {
         v = selectorMtf[i];
         tmp = pos[v];
         while (v > 0) { pos[v] = pos[v-1]; v--; }
         pos[0] = tmp;
         selector[i] = tmp;
      }
   }

   /*--- Now the coding tables ---*/
   for (t = 0; t < nGroups; t++) {
      Int32 curr = bsR ( 5 );
      for (i = 0; i < alphaSize; i++) {
         while (bsR(1) == 1) {
            if (bsR(1) == 0) curr++; else curr--;
         }
         len[t][i] = curr;
      }
   }

   /*--- Create the Huffman decoding tables ---*/
   for (t = 0; t < nGroups; t++) {
      minLen = 32;
      maxLen = 0;
      for (i = 0; i < alphaSize; i++) {
         if (len[t][i] > maxLen) maxLen = len[t][i];
         if (len[t][i] < minLen) minLen = len[t][i];
      }
      hbCreateDecodeTables ( 
         &limit[t][0], &base[t][0], &perm[t][0], &len[t][0],
         minLen, maxLen, alphaSize
      );
      minLens[t] = minLen;
   }
}


/*---------------------------------------------*/
#define GET_MTF_VAL(lval)                 \
{                                         \
   Int32 zt, zn, zvec, zj;                \
   if (groupPos == 0) {                   \
      groupNo++;                          \
      groupPos = G_SIZE;                  \
   }                                      \
   groupPos--;                            \
   zt = selector[groupNo];                \
   zn = minLens[zt];                      \
   zvec = bsR ( zn );                     \
   while (zvec > limit[zt][zn]) {         \
      zn++; bsR1(zj);                     \
      zvec = (zvec << 1) | zj;            \
   };                                     \
   lval = perm[zt][zvec - base[zt][zn]];  \
}


/*---------------------------------------------*/
void getAndMoveToFrontDecode ( void )
{
   UChar  yy[256];
   Int32  i, j, nextSym, limitLast;
   Int32  EOB, groupNo, groupPos;

   limitLast = 100000 * blockSize100k;
   origPtr   = bsGetIntVS ( 24 );

   recvDecodingTables();
   EOB      = nInUse+1;
   groupNo  = -1;
   groupPos = 0;

   /*--
      Setting up the unzftab entries here is not strictly
      necessary, but it does save having to do it later
      in a separate pass, and so saves a block's worth of
      cache misses.
   --*/
   for (i = 0; i <= 255; i++) unzftab[i] = 0;

   for (i = 0; i <= 255; i++) yy[i] = (UChar) i;

   last = -1;

   GET_MTF_VAL(nextSym);

   while (True) {

      if (nextSym == EOB) break;

      if (nextSym == RUNA || nextSym == RUNB) {
         UChar ch;
         Int32 s = -1;
         Int32 N = 1;
         do {
            if (nextSym == RUNA) s = s + (0+1) * N; else
            if (nextSym == RUNB) s = s + (1+1) * N;
            N = N * 2;
            GET_MTF_VAL(nextSym);
         }
            while (nextSym == RUNA || nextSym == RUNB);

         s++;
         ch = seqToUnseq[yy[0]];
         unzftab[ch] += s;

         if (smallMode)
            while (s > 0) {
               last++; 
               ll16[last] = ch;
               s--;
            }
         else
            while (s > 0) {
               last++;
               ll8[last] = ch;
               s--;
            };

         if (last >= limitLast) blockOverrun();
         continue;

      } else {

         UChar tmp;
         last++; if (last >= limitLast) blockOverrun();

         tmp = yy[nextSym-1];
         unzftab[seqToUnseq[tmp]]++;
         if (smallMode)
            ll16[last] = seqToUnseq[tmp]; else
            ll8[last]  = seqToUnseq[tmp];

         /*--
            This loop is hammered during decompression,
            hence the unrolling.

            for (j = nextSym-1; j > 0; j--) yy[j] = yy[j-1];
         --*/

         j = nextSym-1;
         for (; j > 3; j -= 4) {
            yy[j]   = yy[j-1];
            yy[j-1] = yy[j-2];
            yy[j-2] = yy[j-3];
            yy[j-3] = yy[j-4];
         }
         for (; j > 0; j--) yy[j] = yy[j-1];

         yy[0] = tmp;
         GET_MTF_VAL(nextSym);
         continue;
      }
   }
}


/*---------------------------------------------------*/
/*--- Block-sorting machinery                     ---*/
/*---------------------------------------------------*/

/*---------------------------------------------*/
/*--
  Compare two strings in block.  We assume (see
  discussion above) that i1 and i2 have a max
  offset of 10 on entry, and that the first
  bytes of both block and quadrant have been
  copied into the "overshoot area", ie
  into the subscript range
  [last+1 .. last+NUM_OVERSHOOT_BYTES].
--*/
INLINE Bool fullGtU ( Int32 i1, Int32 i2 )
{
   Int32 k;
   UChar c1, c2;
   UInt16 s1, s2;

   #if DEBUG
      /*--
        shellsort shouldn't ask to compare
        something with itself.
      --*/
      assert (i1 != i2);
   #endif

   c1 = block[i1];
   c2 = block[i2];
   if (c1 != c2) return (c1 > c2);
   i1++; i2++;

   c1 = block[i1];
   c2 = block[i2];
   if (c1 != c2) return (c1 > c2);
   i1++; i2++;

   c1 = block[i1];
   c2 = block[i2];
   if (c1 != c2) return (c1 > c2);
   i1++; i2++;

   c1 = block[i1];
   c2 = block[i2];
   if (c1 != c2) return (c1 > c2);
   i1++; i2++;

   c1 = block[i1];
   c2 = block[i2];
   if (c1 != c2) return (c1 > c2);
   i1++; i2++;

   c1 = block[i1];
   c2 = block[i2];
   if (c1 != c2) return (c1 > c2);
   i1++; i2++;

   k = last + 1;

   do {

      c1 = block[i1];
      c2 = block[i2];
      if (c1 != c2) return (c1 > c2);
      s1 = quadrant[i1];
      s2 = quadrant[i2];
      if (s1 != s2) return (s1 > s2);
      i1++; i2++;

      c1 = block[i1];
      c2 = block[i2];
      if (c1 != c2) return (c1 > c2);
      s1 = quadrant[i1];
      s2 = quadrant[i2];
      if (s1 != s2) return (s1 > s2);
      i1++; i2++;

      c1 = block[i1];
      c2 = block[i2];
      if (c1 != c2) return (c1 > c2);
      s1 = quadrant[i1];
      s2 = quadrant[i2];
      if (s1 != s2) return (s1 > s2);
      i1++; i2++;

      c1 = block[i1];
      c2 = block[i2];
      if (c1 != c2) return (c1 > c2);
      s1 = quadrant[i1];
      s2 = quadrant[i2];
      if (s1 != s2) return (s1 > s2);
      i1++; i2++;

      if (i1 > last) { i1 -= last; i1--; };
      if (i2 > last) { i2 -= last; i2--; };

      k -= 4;
      workDone++;
   }
      while (k >= 0);

   return False;
}

/*---------------------------------------------*/
/*--
   Knuth's increments seem to work better
   than Incerpi-Sedgewick here.  Possibly
   because the number of elems to sort is
   usually small, typically <= 20.
--*/
Int32 incs[14] = { 1, 4, 13, 40, 121, 364, 1093, 3280,
                   9841, 29524, 88573, 265720,
                   797161, 2391484 };

void simpleSort ( Int32 lo, Int32 hi, Int32 d )
{
   Int32 i, j, h, bigN, hp;
   Int32 v;

   bigN = hi - lo + 1;
   if (bigN < 2) return;

   hp = 0;
   while (incs[hp] < bigN) hp++;
   hp--;

   for (; hp >= 0; hp--) {
      h = incs[hp];
      if (verbosity >= 5) 
         fprintf ( stderr, "          shell increment %d\n", h );

      i = lo + h;
      while (True) {

         /*-- copy 1 --*/
         if (i > hi) break;
         v = zptr[i];
         j = i;
         while ( fullGtU ( zptr[j-h]+d, v+d ) ) {
            zptr[j] = zptr[j-h];
            j = j - h;
            if (j <= (lo + h - 1)) break;
         }
         zptr[j] = v;
         i++;

         /*-- copy 2 --*/
         if (i > hi) break;
         v = zptr[i];
         j = i;
         while ( fullGtU ( zptr[j-h]+d, v+d ) ) {
            zptr[j] = zptr[j-h];
            j = j - h;
            if (j <= (lo + h - 1)) break;
         }
         zptr[j] = v;
         i++;

         /*-- copy 3 --*/
         if (i > hi) break;
         v = zptr[i];
         j = i;
         while ( fullGtU ( zptr[j-h]+d, v+d ) ) {
            zptr[j] = zptr[j-h];
            j = j - h;
            if (j <= (lo + h - 1)) break;
         }
         zptr[j] = v;
         i++;

         if (workDone > workLimit && firstAttempt) return;
      }
   }
}


/*---------------------------------------------*/
/*--
   The following is an implementation of
   an elegant 3-way quicksort for strings,
   described in a paper "Fast Algorithms for
   Sorting and Searching Strings", by Robert
   Sedgewick and Jon L. Bentley.
--*/

#define swap(lv1, lv2) \
   { Int32 tmp = lv1; lv1 = lv2; lv2 = tmp; }

INLINE void vswap ( Int32 p1, Int32 p2, Int32 n )
{
   while (n > 0) {
      swap(zptr[p1], zptr[p2]);
      p1++; p2++; n--;
   }
}

INLINE UChar med3 ( UChar a, UChar b, UChar c )
{
   UChar t;
   if (a > b) { t = a; a = b; b = t; };
   if (b > c) { t = b; b = c; c = t; };
   if (a > b)          b = a;
   return b;
}


#define min(a,b) ((a) < (b)) ? (a) : (b)

typedef
   struct { Int32 ll; Int32 hh; Int32 dd; }
   StackElem;

#define push(lz,hz,dz) { stack[sp].ll = lz; \
                         stack[sp].hh = hz; \
                         stack[sp].dd = dz; \
                         sp++; }

#define pop(lz,hz,dz) { sp--;               \
                        lz = stack[sp].ll;  \
                        hz = stack[sp].hh;  \
                        dz = stack[sp].dd; }

#define SMALL_THRESH 20
#define DEPTH_THRESH 10

/*--
   If you are ever unlucky/improbable enough
   to get a stack overflow whilst sorting,
   increase the following constant and try
   again.  In practice I have never seen the
   stack go above 27 elems, so the following
   limit seems very generous.
--*/
#define QSORT_STACK_SIZE 1000


void qSort3 ( Int32 loSt, Int32 hiSt, Int32 dSt )
{
   Int32 unLo, unHi, ltLo, gtHi, med, n, m;
   Int32 sp, lo, hi, d;
   StackElem stack[QSORT_STACK_SIZE];

   sp = 0;
   push ( loSt, hiSt, dSt );

   while (sp > 0) {

      if (sp >= QSORT_STACK_SIZE) panic ( "stack overflow in qSort3" );

      pop ( lo, hi, d );

      if (hi - lo < SMALL_THRESH || d > DEPTH_THRESH) {
         simpleSort ( lo, hi, d );
         if (workDone > workLimit && firstAttempt) return;
         continue;
      }

      med = med3 ( block[zptr[ lo         ]+d],
                   block[zptr[ hi         ]+d],
                   block[zptr[ (lo+hi)>>1 ]+d] );

      unLo = ltLo = lo;
      unHi = gtHi = hi;

      while (True) {
         while (True) {
            if (unLo > unHi) break;
            n = ((Int32)block[zptr[unLo]+d]) - med;
            if (n == 0) { swap(zptr[unLo], zptr[ltLo]); ltLo++; unLo++; continue; };
            if (n >  0) break;
            unLo++;
         }
         while (True) {
            if (unLo > unHi) break;
            n = ((Int32)block[zptr[unHi]+d]) - med;
            if (n == 0) { swap(zptr[unHi], zptr[gtHi]); gtHi--; unHi--; continue; };
            if (n <  0) break;
            unHi--;
         }
         if (unLo > unHi) break;
         swap(zptr[unLo], zptr[unHi]); unLo++; unHi--;
      }
      #if DEBUG
         assert (unHi == unLo-1);
      #endif

      if (gtHi < ltLo) {
         push(lo, hi, d+1 );
         continue;
      }

      n = min(ltLo-lo, unLo-ltLo); vswap(lo, unLo-n, n);
      m = min(hi-gtHi, gtHi-unHi); vswap(unLo, hi-m+1, m);

      n = lo + unLo - ltLo - 1;
      m = hi - (gtHi - unHi) + 1;

      push ( lo, n, d );
      push ( n+1, m-1, d+1 );
      push ( m, hi, d );
   }
}


/*---------------------------------------------*/

#define BIGFREQ(b) (ftab[((b)+1) << 8] - ftab[(b) << 8])

#define SETMASK (1 << 21)
#define CLEARMASK (~(SETMASK))

void sortIt ( void )
{
   Int32 i, j, ss, sb;
   Int32 runningOrder[256];
   Int32 copy[256];
   Bool bigDone[256];
   UChar c1, c2;
   Int32 numQSorted;

   /*--
      In the various block-sized structures, live data runs
      from 0 to last+NUM_OVERSHOOT_BYTES inclusive.  First,
      set up the overshoot area for block.
   --*/

   if (verbosity >= 4) fprintf ( stderr, "        sort initialise ...\n" );
   for (i = 0; i < NUM_OVERSHOOT_BYTES; i++)
       block[last+i+1] = block[i % (last+1)];
   for (i = 0; i <= last+NUM_OVERSHOOT_BYTES; i++)
       quadrant[i] = 0;

   block[-1] = block[last];

   if (last < 4000) {

      /*--
         Use simpleSort(), since the full sorting mechanism
         has quite a large constant overhead.
      --*/
      if (verbosity >= 4) fprintf ( stderr, "        simpleSort ...\n" );
      for (i = 0; i <= last; i++) zptr[i] = i;
      firstAttempt = False;
      workDone = workLimit = 0;
      simpleSort ( 0, last, 0 );
      if (verbosity >= 4) fprintf ( stderr, "        simpleSort done.\n" );

   } else {

      numQSorted = 0;
      for (i = 0; i <= 255; i++) bigDone[i] = False;

      if (verbosity >= 4) fprintf ( stderr, "        bucket sorting ...\n" );

      for (i = 0; i <= 65536; i++) ftab[i] = 0;

      c1 = block[-1];
      for (i = 0; i <= last; i++) {
         c2 = block[i];
         ftab[(c1 << 8) + c2]++;
         c1 = c2;
      }

      for (i = 1; i <= 65536; i++) ftab[i] += ftab[i-1];

      c1 = block[0];
      for (i = 0; i < last; i++) {
         c2 = block[i+1];
         j = (c1 << 8) + c2;
         c1 = c2;
         ftab[j]--;
         zptr[ftab[j]] = i;
      }
      j = (block[last] << 8) + block[0];
      ftab[j]--;
      zptr[ftab[j]] = last;

      /*--
         Now ftab contains the first loc of every small bucket.
         Calculate the running order, from smallest to largest
         big bucket.
      --*/

      for (i = 0; i <= 255; i++) runningOrder[i] = i;

      {
         Int32 vv;
         Int32 h = 1;
         do h = 3 * h + 1; while (h <= 256);
         do {
            h = h / 3;
            for (i = h; i <= 255; i++) {
               vv = runningOrder[i];
               j = i;
               while ( BIGFREQ(runningOrder[j-h]) > BIGFREQ(vv) ) {
                  runningOrder[j] = runningOrder[j-h];
                  j = j - h;
                  if (j <= (h - 1)) goto zero;
               }
               zero:
               runningOrder[j] = vv;
            }
         } while (h != 1);
      }

      /*--
         The main sorting loop.
      --*/

      for (i = 0; i <= 255; i++) {

         /*--
            Process big buckets, starting with the least full.
         --*/
         ss = runningOrder[i];

         /*--
            Complete the big bucket [ss] by quicksorting
            any unsorted small buckets [ss, j].  Hopefully
            previous pointer-scanning phases have already
            completed many of the small buckets [ss, j], so
            we don't have to sort them at all.
         --*/
         for (j = 0; j <= 255; j++) {
            sb = (ss << 8) + j;
            if ( ! (ftab[sb] & SETMASK) ) {
               Int32 lo = ftab[sb]   & CLEARMASK;
               Int32 hi = (ftab[sb+1] & CLEARMASK) - 1;
               if (hi > lo) {
                  if (verbosity >= 4)
                     fprintf ( stderr,
                               "        qsort [0x%x, 0x%x]   done %d   this %d\n",
                               ss, j, numQSorted, hi - lo + 1 );
                  qSort3 ( lo, hi, 2 );
                  numQSorted += ( hi - lo + 1 );
                  if (workDone > workLimit && firstAttempt) return;
               }
               ftab[sb] |= SETMASK;
            }
         }

         /*--
            The ss big bucket is now done.  Record this fact,
            and update the quadrant descriptors.  Remember to
            update quadrants in the overshoot area too, if
            necessary.  The "if (i < 255)" test merely skips
            this updating for the last bucket processed, since
            updating for the last bucket is pointless.
         --*/
         bigDone[ss] = True;

         if (i < 255) {
            Int32 bbStart  = ftab[ss << 8] & CLEARMASK;
            Int32 bbSize   = (ftab[(ss+1) << 8] & CLEARMASK) - bbStart;
            Int32 shifts   = 0;

            while ((bbSize >> shifts) > 65534) shifts++;

            for (j = 0; j < bbSize; j++) {
               Int32 a2update     = zptr[bbStart + j];
               UInt16 qVal        = (UInt16)(j >> shifts);
               quadrant[a2update] = qVal;
               if (a2update < NUM_OVERSHOOT_BYTES)
                  quadrant[a2update + last + 1] = qVal;
            }

            if (! ( ((bbSize-1) >> shifts) <= 65535 )) panic ( "sortIt" );
         }

         /*--
            Now scan this big bucket so as to synthesise the
            sorted order for small buckets [t, ss] for all t != ss.
         --*/
         for (j = 0; j <= 255; j++)
            copy[j] = ftab[(j << 8) + ss] & CLEARMASK;

         for (j = ftab[ss << 8] & CLEARMASK;
              j < (ftab[(ss+1) << 8] & CLEARMASK);
              j++) {
            c1 = block[zptr[j]-1];
            if ( ! bigDone[c1] ) {
               zptr[copy[c1]] = zptr[j] == 0 ? last : zptr[j] - 1;
               copy[c1] ++;
            }
         }

         for (j = 0; j <= 255; j++) ftab[(j << 8) + ss] |= SETMASK;
      }
      if (verbosity >= 4)
         fprintf ( stderr, "        %d pointers, %d sorted, %d scanned\n",
                           last+1, numQSorted, (last+1) - numQSorted );
   }
}


/*---------------------------------------------------*/
/*--- Stuff for randomising repetitive blocks     ---*/
/*---------------------------------------------------*/

/*---------------------------------------------*/
Int32 rNums[512] = { 
   619, 720, 127, 481, 931, 816, 813, 233, 566, 247, 
   985, 724, 205, 454, 863, 491, 741, 242, 949, 214, 
   733, 859, 335, 708, 621, 574, 73, 654, 730, 472, 
   419, 436, 278, 496, 867, 210, 399, 680, 480, 51, 
   878, 465, 811, 169, 869, 675, 611, 697, 867, 561, 
   862, 687, 507, 283, 482, 129, 807, 591, 733, 623, 
   150, 238, 59, 379, 684, 877, 625, 169, 643, 105, 
   170, 607, 520, 932, 727, 476, 693, 425, 174, 647, 
   73, 122, 335, 530, 442, 853, 695, 249, 445, 515, 
   909, 545, 703, 919, 874, 474, 882, 500, 594, 612, 
   641, 801, 220, 162, 819, 984, 589, 513, 495, 799, 
   161, 604, 958, 533, 221, 400, 386, 867, 600, 782, 
   382, 596, 414, 171, 516, 375, 682, 485, 911, 276, 
   98, 553, 163, 354, 666, 933, 424, 341, 533, 870, 
   227, 730, 475, 186, 263, 647, 537, 686, 600, 224, 
   469, 68, 770, 919, 190, 373, 294, 822, 808, 206, 
   184, 943, 795, 384, 383, 461, 404, 758, 839, 887, 
   715, 67, 618, 276, 204, 918, 873, 777, 604, 560, 
   951, 160, 578, 722, 79, 804, 96, 409, 713, 940, 
   652, 934, 970, 447, 318, 353, 859, 672, 112, 785, 
   645, 863, 803, 350, 139, 93, 354, 99, 820, 908, 
   609, 772, 154, 274, 580, 184, 79, 626, 630, 742, 
   653, 282, 762, 623, 680, 81, 927, 626, 789, 125, 
   411, 521, 938, 300, 821, 78, 343, 175, 128, 250, 
   170, 774, 972, 275, 999, 639, 495, 78, 352, 126, 
   857, 956, 358, 619, 580, 124, 737, 594, 701, 612, 
   669, 112, 134, 694, 363, 992, 809, 743, 168, 974, 
   944, 375, 748, 52, 600, 747, 642, 182, 862, 81, 
   344, 805, 988, 739, 511, 655, 814, 334, 249, 515, 
   897, 955, 664, 981, 649, 113, 974, 459, 893, 228, 
   433, 837, 553, 268, 926, 240, 102, 654, 459, 51, 
   686, 754, 806, 760, 493, 403, 415, 394, 687, 700, 
   946, 670, 656, 610, 738, 392, 760, 799, 887, 653, 
   978, 321, 576, 617, 626, 502, 894, 679, 243, 440, 
   680, 879, 194, 572, 640, 724, 926, 56, 204, 700, 
   707, 151, 457, 449, 797, 195, 791, 558, 945, 679, 
   297, 59, 87, 824, 713, 663, 412, 693, 342, 606, 
   134, 108, 571, 364, 631, 212, 174, 643, 304, 329, 
   343, 97, 430, 751, 497, 314, 983, 374, 822, 928, 
   140, 206, 73, 263, 980, 736, 876, 478, 430, 305, 
   170, 514, 364, 692, 829, 82, 855, 953, 676, 246, 
   369, 970, 294, 750, 807, 827, 150, 790, 288, 923, 
   804, 378, 215, 828, 592, 281, 565, 555, 710, 82, 
   896, 831, 547, 261, 524, 462, 293, 465, 502, 56, 
   661, 821, 976, 991, 658, 869, 905, 758, 745, 193, 
   768, 550, 608, 933, 378, 286, 215, 979, 792, 961, 
   61, 688, 793, 644, 986, 403, 106, 366, 905, 644, 
   372, 567, 466, 434, 645, 210, 389, 550, 919, 135, 
   780, 773, 635, 389, 707, 100, 626, 958, 165, 504, 
   920, 176, 193, 713, 857, 265, 203, 50, 668, 108, 
   645, 990, 626, 197, 510, 357, 358, 850, 858, 364, 
   936, 638
};


#define RAND_DECLS                                \
   Int32 rNToGo = 0;                              \
   Int32 rTPos  = 0;                              \

#define RAND_MASK ((rNToGo == 1) ? 1 : 0)

#define RAND_UPD_MASK                             \
   if (rNToGo == 0) {                             \
      rNToGo = rNums[rTPos];                      \
      rTPos++; if (rTPos == 512) rTPos = 0;       \
   }                                              \
   rNToGo--;



/*---------------------------------------------------*/
/*--- The Reversible Transformation (tm)          ---*/
/*---------------------------------------------------*/

/*---------------------------------------------*/
void randomiseBlock ( void )
{
   Int32 i;
   RAND_DECLS;
   for (i = 0; i < 256; i++) inUse[i] = False;

   for (i = 0; i <= last; i++) {
      RAND_UPD_MASK;
      block[i] ^= RAND_MASK;
      inUse[block[i]] = True;
   }
}


/*---------------------------------------------*/
void doReversibleTransformation ( void )
{
   Int32 i;

   if (verbosity >= 2) fprintf ( stderr, "\n" );

   workLimit       = workFactor * last;
   workDone        = 0;
   blockRandomised = False;
   firstAttempt    = True;

   sortIt ();

   if (verbosity >= 3)
      fprintf ( stderr, "      %d work, %d block, ratio %5.2f\n",
                        workDone, last, (float)workDone / (float)(last) );

   if (workDone > workLimit && firstAttempt) {
      if (verbosity >= 2)
         fprintf ( stderr, "    sorting aborted; randomising block\n" );
      randomiseBlock ();
      workLimit = workDone = 0;
      blockRandomised = True;
      firstAttempt = False;
      sortIt();
      if (verbosity >= 3)
         fprintf ( stderr, "      %d work, %d block, ratio %f\n",
                           workDone, last, (float)workDone / (float)(last) );
   }

   origPtr = -1;
   for (i = 0; i <= last; i++)
       if (zptr[i] == 0)
          { origPtr = i; break; };

   if (origPtr == -1) panic ( "doReversibleTransformation" );
}


/*---------------------------------------------*/

INLINE Int32 indexIntoF ( Int32 indx, Int32 *cftab )
{
   Int32 nb, na, mid;
   nb = 0;
   na = 256;
   do {
      mid = (nb + na) >> 1;
      if (indx >= cftab[mid]) nb = mid; else na = mid;
   }
   while (na - nb != 1);
   return nb;
}


#define GET_SMALL(cccc)                     \
                                            \
      cccc = indexIntoF ( tPos, cftab );    \
      tPos = GET_LL(tPos);


void undoReversibleTransformation_small ( FILE* dst )
{
   Int32  cftab[257], cftabAlso[257];
   Int32  i, j, tmp, tPos;
   UChar  ch;

   /*--
      We assume here that the global array unzftab will
      already be holding the frequency counts for
      ll8[0 .. last].
   --*/

   /*-- Set up cftab to facilitate generation of indexIntoF --*/
   cftab[0] = 0;
   for (i = 1; i <= 256; i++) cftab[i] = unzftab[i-1];
   for (i = 1; i <= 256; i++) cftab[i] += cftab[i-1];

   /*-- Make a copy of it, used in generation of T --*/
   for (i = 0; i <= 256; i++) cftabAlso[i] = cftab[i];

   /*-- compute the T vector --*/
   for (i = 0; i <= last; i++) {
      ch = (UChar)ll16[i];
      SET_LL(i, cftabAlso[ch]);
      cftabAlso[ch]++;
   }

   /*--
      Compute T^(-1) by pointer reversal on T.  This is rather
      subtle, in that, if the original block was two or more
      (in general, N) concatenated copies of the same thing,
      the T vector will consist of N cycles, each of length
      blocksize / N, and decoding will involve traversing one
      of these cycles N times.  Which particular cycle doesn't
      matter -- they are all equivalent.  The tricky part is to
      make sure that the pointer reversal creates a correct
      reversed cycle for us to traverse.  So, the code below
      simply reverses whatever cycle origPtr happens to fall into,
      without regard to the cycle length.  That gives one reversed
      cycle, which for normal blocks, is the entire block-size long.
      For repeated blocks, it will be interspersed with the other
      N-1 non-reversed cycles.  Providing that the F-subscripting
      phase which follows starts at origPtr, all then works ok.
   --*/
   i = origPtr;
   j = GET_LL(i);
   do {
      tmp = GET_LL(j);
      SET_LL(j, i);
      i = j;
      j = tmp;
   }
      while (i != origPtr);

   /*--
      We recreate the original by subscripting F through T^(-1).
      The run-length-decoder below requires characters incrementally,
      so tPos is set to a starting value, and is updated by
      the GET_SMALL macro.
   --*/
   tPos   = origPtr;

   /*-------------------------------------------------*/
   /*--
      This is pretty much a verbatim copy of the
      run-length decoder present in the distribution
      bzip-0.21; it has to be here to avoid creating
      block[] as an intermediary structure.  As in 0.21,
      this code derives from some sent to me by
      Christian von Roques.

      It allows dst==NULL, so as to support the test (-t)
      option without slowing down the fast decompression
      code.
   --*/
   {
      IntNative retVal;
      Int32     i2, count, chPrev, ch2;
      UInt32    localCrc;

      count    = 0;
      i2       = 0;
      ch2      = 256;   /*-- not a char and not EOF --*/
      localCrc = getGlobalCRC();

      {
         RAND_DECLS;
         while ( i2 <= last ) {
            chPrev = ch2;
            GET_SMALL(ch2);
            if (blockRandomised) {
               RAND_UPD_MASK;
               ch2 ^= (UInt32)RAND_MASK;
            }
            i2++;
   
            if (dst)
               retVal = putc ( ch2, dst );
   
            UPDATE_CRC ( localCrc, (UChar)ch2 );
   
            if (ch2 != chPrev) {
               count = 1;
            } else {
               count++;
               if (count >= 4) {
                  Int32 j2;
                  UChar z;
                  GET_SMALL(z);
                  if (blockRandomised) {
                     RAND_UPD_MASK;
                     z ^= RAND_MASK;
                  }
                  for (j2 = 0;  j2 < (Int32)z;  j2++) {
                     if (dst) retVal = putc (ch2, dst);
                     UPDATE_CRC ( localCrc, (UChar)ch2 );
                  }
                  i2++;
                  count = 0;
               }
            }
         }
      }

      setGlobalCRC ( localCrc );
   }
   /*-- end of the in-line run-length-decoder. --*/
}
#undef GET_SMALL


/*---------------------------------------------*/

#define GET_FAST(cccc)                       \
                                             \
      cccc = ll8[tPos];                      \
      tPos = tt[tPos];


void undoReversibleTransformation_fast ( FILE* dst )
{
   Int32  cftab[257];
   Int32  i, tPos;
   UChar  ch;

   /*--
      We assume here that the global array unzftab will
      already be holding the frequency counts for
      ll8[0 .. last].
   --*/

   /*-- Set up cftab to facilitate generation of T^(-1) --*/
   cftab[0] = 0;
   for (i = 1; i <= 256; i++) cftab[i] = unzftab[i-1];
   for (i = 1; i <= 256; i++) cftab[i] += cftab[i-1];

   /*-- compute the T^(-1) vector --*/
   for (i = 0; i <= last; i++) {
      ch = (UChar)ll8[i];
      tt[cftab[ch]] = i;
      cftab[ch]++;
   }

   /*--
      We recreate the original by subscripting L through T^(-1).
      The run-length-decoder below requires characters incrementally,
      so tPos is set to a starting value, and is updated by
      the GET_FAST macro.
   --*/
   tPos   = tt[origPtr];

   /*-------------------------------------------------*/
   /*--
      This is pretty much a verbatim copy of the
      run-length decoder present in the distribution
      bzip-0.21; it has to be here to avoid creating
      block[] as an intermediary structure.  As in 0.21,
      this code derives from some sent to me by
      Christian von Roques.
   --*/
   {
      IntNative retVal;
      Int32     i2, count, chPrev, ch2;
      UInt32    localCrc;

      count    = 0;
      i2       = 0;
      ch2      = 256;   /*-- not a char and not EOF --*/
      localCrc = getGlobalCRC();

      if (blockRandomised) {
         RAND_DECLS;
         while ( i2 <= last ) {
            chPrev = ch2;
            GET_FAST(ch2);
            RAND_UPD_MASK;
            ch2 ^= (UInt32)RAND_MASK;
            i2++;
   
            retVal = putc ( ch2, dst );
            UPDATE_CRC ( localCrc, (UChar)ch2 );
   
            if (ch2 != chPrev) {
               count = 1;
            } else {
               count++;
               if (count >= 4) {
                  Int32 j2;
                  UChar z;
                  GET_FAST(z);
                  RAND_UPD_MASK;
                  z ^= RAND_MASK;
                  for (j2 = 0;  j2 < (Int32)z;  j2++) {
                     retVal = putc (ch2, dst);
                     UPDATE_CRC ( localCrc, (UChar)ch2 );
                  }
                  i2++;
                  count = 0;
               }
            }
         }

      } else {

         while ( i2 <= last ) {
            chPrev = ch2;
            GET_FAST(ch2);
            i2++;
   
            retVal = putc ( ch2, dst );
            UPDATE_CRC ( localCrc, (UChar)ch2 );
   
            if (ch2 != chPrev) {
               count = 1;
            } else {
               count++;
               if (count >= 4) {
                  Int32 j2;
                  UChar z;
                  GET_FAST(z);
                  for (j2 = 0;  j2 < (Int32)z;  j2++) {
                     retVal = putc (ch2, dst);
                     UPDATE_CRC ( localCrc, (UChar)ch2 );
                  }
                  i2++;
                  count = 0;
               }
            }
         }

      }   /*-- if (blockRandomised) --*/

      setGlobalCRC ( localCrc );
   }
   /*-- end of the in-line run-length-decoder. --*/
}
#undef GET_FAST


/*---------------------------------------------------*/
/*--- The block loader and RLEr                   ---*/
/*---------------------------------------------------*/

/*---------------------------------------------*/
/*  Top 16:   run length, 1 to 255.
*   Lower 16: the char, or MY_EOF for EOF.
*/

#define MY_EOF 257

INLINE Int32 getRLEpair ( FILE* src )
{
   Int32     runLength;
   IntNative ch, chLatest;

   ch = getc ( src );

   /*--- Because I have no idea what kind of a value EOF is. ---*/
   if (ch == EOF) {
      ERROR_IF_NOT_ZERO ( ferror(src));
      return (1 << 16) | MY_EOF;
   }

   runLength = 0;
   do {
      chLatest = getc ( src );
      runLength++;
      bytesIn++;
   }
      while (ch == chLatest && runLength < 255);

   if ( chLatest != EOF ) {
      if ( ungetc ( chLatest, src ) == EOF )
         panic ( "getRLEpair: ungetc failed" );
   } else {
      ERROR_IF_NOT_ZERO ( ferror(src) );
   }

   /*--- Conditional is just a speedup hack. ---*/
   if (runLength == 1) {
      UPDATE_CRC ( globalCrc, (UChar)ch );
      return (1 << 16) | ch;
   } else {
      Int32 i;
      for (i = 1; i <= runLength; i++)
         UPDATE_CRC ( globalCrc, (UChar)ch );
      return (runLength << 16) | ch;
   }
}


/*---------------------------------------------*/
void loadAndRLEsource ( FILE* src )
{
   Int32 ch, allowableBlockSize, i;

   last = -1;
   ch   = 0;

   for (i = 0; i < 256; i++) inUse[i] = False;

   /*--- 20 is just a paranoia constant ---*/
   allowableBlockSize = 100000 * blockSize100k - 20;

   while (last < allowableBlockSize && ch != MY_EOF) {
      Int32 rlePair, runLen;
      rlePair = getRLEpair ( src );
      ch      = rlePair & 0xFFFF;
      runLen  = (UInt32)rlePair >> 16;

      #if DEBUG
         assert (runLen >= 1 && runLen <= 255);
      #endif

      if (ch != MY_EOF) {
         inUse[ch] = True;
         switch (runLen) {
            case 1:
               last++; block[last] = (UChar)ch; break;
            case 2:
               last++; block[last] = (UChar)ch;
               last++; block[last] = (UChar)ch; break;
            case 3:
               last++; block[last] = (UChar)ch;
               last++; block[last] = (UChar)ch;
               last++; block[last] = (UChar)ch; break;
            default:
               inUse[runLen-4] = True;
               last++; block[last] = (UChar)ch;
               last++; block[last] = (UChar)ch;
               last++; block[last] = (UChar)ch;
               last++; block[last] = (UChar)ch;
               last++; block[last] = (UChar)(runLen-4); break;
         }
      }
   }
}


/*---------------------------------------------------*/
/*--- Processing of complete files and streams    ---*/
/*---------------------------------------------------*/

/*---------------------------------------------*/
void compressStream ( FILE *stream, FILE *zStream )
{
   IntNative  retVal;
   UInt32     blockCRC, combinedCRC;
   Int32      blockNo;

   blockNo  = 0;
   bytesIn  = 0;
   bytesOut = 0;
   nBlocksRandomised = 0;

   SET_BINARY_MODE(stream);
   SET_BINARY_MODE(zStream);

   ERROR_IF_NOT_ZERO ( ferror(stream) );
   ERROR_IF_NOT_ZERO ( ferror(zStream) );

   bsSetStream ( zStream, True );

   /*--- Write `magic' bytes B and Z,
         then h indicating file-format == huffmanised,
         followed by a digit indicating blockSize100k.
   ---*/
   bsPutUChar ( 'B' );
   bsPutUChar ( 'Z' );
   bsPutUChar ( 'h' );
   bsPutUChar ( '0' + blockSize100k );

   combinedCRC = 0;

   if (verbosity >= 2) fprintf ( stderr, "\n" );

   while (True) {

      blockNo++;
      initialiseCRC ();
      loadAndRLEsource ( stream );
      ERROR_IF_NOT_ZERO ( ferror(stream) );
      if (last == -1) break;

      blockCRC = getFinalCRC ();
      combinedCRC = (combinedCRC << 1) | (combinedCRC >> 31);
      combinedCRC ^= blockCRC;

      if (verbosity >= 2)
         fprintf ( stderr, "    block %d: crc = 0x%8x, combined CRC = 0x%8x, size = %d",
                           blockNo, blockCRC, combinedCRC, last+1 );

      /*-- sort the block and establish posn of original string --*/
      doReversibleTransformation ();

      /*--
        A 6-byte block header, the value chosen arbitrarily
        as 0x314159265359 :-).  A 32 bit value does not really
        give a strong enough guarantee that the value will not
        appear by chance in the compressed datastream.  Worst-case
        probability of this event, for a 900k block, is about
        2.0e-3 for 32 bits, 1.0e-5 for 40 bits and 4.0e-8 for 48 bits.
        For a compressed file of size 100Gb -- about 100000 blocks --
        only a 48-bit marker will do.  NB: normal compression/
        decompression do *not* rely on these statistical properties.
        They are only important when trying to recover blocks from
        damaged files.
      --*/
      bsPutUChar ( 0x31 ); bsPutUChar ( 0x41 );
      bsPutUChar ( 0x59 ); bsPutUChar ( 0x26 );
      bsPutUChar ( 0x53 ); bsPutUChar ( 0x59 );

      /*-- Now the block's CRC, so it is in a known place. --*/
      bsPutUInt32 ( blockCRC );

      /*-- Now a single bit indicating randomisation. --*/
      if (blockRandomised) {
         bsW(1,1); nBlocksRandomised++;
      } else
         bsW(1,0);

      /*-- Finally, block's contents proper. --*/
      moveToFrontCodeAndSend ();

      ERROR_IF_NOT_ZERO ( ferror(zStream) );
   }

   if (verbosity >= 2 && nBlocksRandomised > 0)
      fprintf ( stderr, "    %d block%s needed randomisation\n", 
                        nBlocksRandomised,
                        nBlocksRandomised == 1 ? "" : "s" );

   /*--
      Now another magic 48-bit number, 0x177245385090, to
      indicate the end of the last block.  (sqrt(pi), if
      you want to know.  I did want to use e, but it contains
      too much repetition -- 27 18 28 18 28 46 -- for me
      to feel statistically comfortable.  Call me paranoid.)
   --*/

   bsPutUChar ( 0x17 ); bsPutUChar ( 0x72 );
   bsPutUChar ( 0x45 ); bsPutUChar ( 0x38 );
   bsPutUChar ( 0x50 ); bsPutUChar ( 0x90 );

   bsPutUInt32 ( combinedCRC );
   if (verbosity >= 2)
      fprintf ( stderr, "    final combined CRC = 0x%x\n   ", combinedCRC );

   /*-- Close the files in an utterly paranoid way. --*/
   bsFinishedWithStream ();

   ERROR_IF_NOT_ZERO ( ferror(zStream) );
   retVal = fflush ( zStream );
   ERROR_IF_EOF ( retVal );
   retVal = fclose ( zStream );
   ERROR_IF_EOF ( retVal );

   ERROR_IF_NOT_ZERO ( ferror(stream) );
   retVal = fclose ( stream );
   ERROR_IF_EOF ( retVal );

   if (bytesIn == 0) bytesIn = 1;
   if (bytesOut == 0) bytesOut = 1;

   if (verbosity >= 1)
      fprintf ( stderr, "%6.3f:1, %6.3f bits/byte, "
                        "%5.2f%% saved, %d in, %d out.\n",
                (float)bytesIn / (float)bytesOut,
                (8.0 * (float)bytesOut) / (float)bytesIn,
                100.0 * (1.0 - (float)bytesOut / (float)bytesIn),
                bytesIn,
                bytesOut
              );
}


/*---------------------------------------------*/
Bool uncompressStream ( FILE *zStream, FILE *stream )
{
   UChar      magic1, magic2, magic3, magic4;
   UChar      magic5, magic6;
   UInt32     storedBlockCRC, storedCombinedCRC;
   UInt32     computedBlockCRC, computedCombinedCRC;
   Int32      currBlockNo;
   IntNative  retVal;

   SET_BINARY_MODE(stream);
   SET_BINARY_MODE(zStream);

   ERROR_IF_NOT_ZERO ( ferror(stream) );
   ERROR_IF_NOT_ZERO ( ferror(zStream) );

   bsSetStream ( zStream, False );

   /*--
      A bad magic number is `recoverable from';
      return with False so the caller skips the file.
   --*/
   magic1 = bsGetUChar ();
   magic2 = bsGetUChar ();
   magic3 = bsGetUChar ();
   magic4 = bsGetUChar ();
   if (magic1 != 'B' ||
       magic2 != 'Z' ||
       magic3 != 'h' ||
       magic4 < '1'  ||
       magic4 > '9') {
     bsFinishedWithStream();
     retVal = fclose ( stream );
     ERROR_IF_EOF ( retVal );
     return False;
   }

   setDecompressStructureSizes ( magic4 - '0' );
   computedCombinedCRC = 0;

   if (verbosity >= 2) fprintf ( stderr, "\n    " );
   currBlockNo = 0;

   while (True) {
      magic1 = bsGetUChar ();
      magic2 = bsGetUChar ();
      magic3 = bsGetUChar ();
      magic4 = bsGetUChar ();
      magic5 = bsGetUChar ();
      magic6 = bsGetUChar ();
      if (magic1 == 0x17 && magic2 == 0x72 &&
          magic3 == 0x45 && magic4 == 0x38 &&
          magic5 == 0x50 && magic6 == 0x90) break;

      if (magic1 != 0x31 || magic2 != 0x41 ||
          magic3 != 0x59 || magic4 != 0x26 ||
          magic5 != 0x53 || magic6 != 0x59) badBlockHeader();

      storedBlockCRC = bsGetUInt32 ();

      if (bsR(1) == 1)
         blockRandomised = True; else
         blockRandomised = False;

      currBlockNo++;
      if (verbosity >= 2)
         fprintf ( stderr, "[%d: huff+mtf ", currBlockNo );
      getAndMoveToFrontDecode ();
      ERROR_IF_NOT_ZERO ( ferror(zStream) );

      initialiseCRC();
      if (verbosity >= 2) fprintf ( stderr, "rt+rld" );
      if (smallMode)
         undoReversibleTransformation_small ( stream );
         else
         undoReversibleTransformation_fast  ( stream );

      ERROR_IF_NOT_ZERO ( ferror(stream) );

      computedBlockCRC = getFinalCRC();
      if (verbosity >= 3)
         fprintf ( stderr, " {0x%x, 0x%x}", storedBlockCRC, computedBlockCRC );
      if (verbosity >= 2) fprintf ( stderr, "] " );

      /*-- A bad CRC is considered a fatal error. --*/
      if (storedBlockCRC != computedBlockCRC)
         crcError ( storedBlockCRC, computedBlockCRC );

      computedCombinedCRC = (computedCombinedCRC << 1) | (computedCombinedCRC >> 31);
      computedCombinedCRC ^= computedBlockCRC;
   };

   if (verbosity >= 2) fprintf ( stderr, "\n    " );

   storedCombinedCRC  = bsGetUInt32 ();
   if (verbosity >= 2)
      fprintf ( stderr,
                "combined CRCs: stored = 0x%x, computed = 0x%x\n    ",
                storedCombinedCRC, computedCombinedCRC );
   if (storedCombinedCRC != computedCombinedCRC)
      crcError ( storedCombinedCRC, computedCombinedCRC );


   bsFinishedWithStream ();
   ERROR_IF_NOT_ZERO ( ferror(zStream) );
   retVal = fclose ( zStream );
   ERROR_IF_EOF ( retVal );

   ERROR_IF_NOT_ZERO ( ferror(stream) );
   retVal = fflush ( stream );
   ERROR_IF_NOT_ZERO ( retVal );
   if (stream != stdout) {
      retVal = fclose ( stream );
      ERROR_IF_EOF ( retVal );
   }
   return True;
}


/*---------------------------------------------*/
Bool testStream ( FILE *zStream )
{
   UChar      magic1, magic2, magic3, magic4;
   UChar      magic5, magic6;
   UInt32     storedBlockCRC, storedCombinedCRC;
   UInt32     computedBlockCRC, computedCombinedCRC;
   Int32      currBlockNo;
   IntNative  retVal;

   SET_BINARY_MODE(zStream);
   ERROR_IF_NOT_ZERO ( ferror(zStream) );

   bsSetStream ( zStream, False );

   magic1 = bsGetUChar ();
   magic2 = bsGetUChar ();
   magic3 = bsGetUChar ();
   magic4 = bsGetUChar ();
   if (magic1 != 'B' ||
       magic2 != 'Z' ||
       magic3 != 'h' ||
       magic4 < '1'  ||
       magic4 > '9') {
     bsFinishedWithStream();
     fclose ( zStream );
     fprintf ( stderr, "\n%s: bad magic number (ie, not created by bzip2)\n",
                       inName );
     return False;
   }

   smallMode = True;
   setDecompressStructureSizes ( magic4 - '0' );
   computedCombinedCRC = 0;

   if (verbosity >= 2) fprintf ( stderr, "\n" );
   currBlockNo = 0;

   while (True) {
      magic1 = bsGetUChar ();
      magic2 = bsGetUChar ();
      magic3 = bsGetUChar ();
      magic4 = bsGetUChar ();
      magic5 = bsGetUChar ();
      magic6 = bsGetUChar ();
      if (magic1 == 0x17 && magic2 == 0x72 &&
          magic3 == 0x45 && magic4 == 0x38 &&
          magic5 == 0x50 && magic6 == 0x90) break;

      currBlockNo++;
      if (magic1 != 0x31 || magic2 != 0x41 ||
          magic3 != 0x59 || magic4 != 0x26 ||
          magic5 != 0x53 || magic6 != 0x59) {
         bsFinishedWithStream();
         fclose ( zStream );
         fprintf ( stderr,
                   "\n%s, block %d: bad header (not == 0x314159265359)\n",
                   inName, currBlockNo );
         return False;
      }
      storedBlockCRC = bsGetUInt32 ();

      if (bsR(1) == 1)
         blockRandomised = True; else
         blockRandomised = False;

      if (verbosity >= 2)
         fprintf ( stderr, "    block [%d: huff+mtf ", currBlockNo );
      getAndMoveToFrontDecode ();
      ERROR_IF_NOT_ZERO ( ferror(zStream) );

      initialiseCRC();
      if (verbosity >= 2) fprintf ( stderr, "rt+rld" );
      undoReversibleTransformation_small ( NULL );

      computedBlockCRC = getFinalCRC();
      if (verbosity >= 3)
         fprintf ( stderr, " {0x%x, 0x%x}", storedBlockCRC, computedBlockCRC );
      if (verbosity >= 2) fprintf ( stderr, "] " );

      if (storedBlockCRC != computedBlockCRC) {
         bsFinishedWithStream();
         fclose ( zStream );
         fprintf ( stderr, "\n%s, block %d: computed CRC does not match stored one\n",
                           inName, currBlockNo );
         return False;
      }

      if (verbosity >= 2) fprintf ( stderr, "ok\n" );
      computedCombinedCRC = (computedCombinedCRC << 1) | (computedCombinedCRC >> 31);
      computedCombinedCRC ^= computedBlockCRC;
   };

   storedCombinedCRC  = bsGetUInt32 ();
   if (verbosity >= 2)
      fprintf ( stderr,
                "    combined CRCs: stored = 0x%x, computed = 0x%x\n    ",
                storedCombinedCRC, computedCombinedCRC );
   if (storedCombinedCRC != computedCombinedCRC) {
      bsFinishedWithStream();
      fclose ( zStream );
      fprintf ( stderr, "\n%s: computed CRC does not match stored one\n",
                        inName );
      return False;
   }

   bsFinishedWithStream ();
   ERROR_IF_NOT_ZERO ( ferror(zStream) );
   retVal = fclose ( zStream );
   ERROR_IF_EOF ( retVal );
   return True;
}



/*---------------------------------------------------*/
/*--- Error [non-] handling grunge                ---*/
/*---------------------------------------------------*/

/*---------------------------------------------*/
void cadvise ( void )
{
   fprintf (
      stderr,
      "\nIt is possible that the compressed file(s) have become corrupted.\n"
        "You can use the -tvv option to test integrity of such files.\n\n"
        "You can use the `bzip2recover' program to *attempt* to recover\n"
        "data from undamaged sections of corrupted files.\n\n"
    );
}


/*---------------------------------------------*/
void showFileNames ( void )
{
   fprintf (
      stderr,
      "\tInput file = %s, output file = %s\n",
      inName==NULL  ? "(null)" : inName,
      outName==NULL ? "(null)" : outName
   );
}


/*---------------------------------------------*/
void cleanUpAndFail ( Int32 ec )
{
   IntNative retVal;

   if ( srcMode == SM_F2F && opMode != OM_TEST ) {
      fprintf ( stderr, "%s: Deleting output file %s, if it exists.\n",
                progName,
                outName==NULL ? "(null)" : outName );
      if (outputHandleJustInCase != NULL)
         fclose ( outputHandleJustInCase );
      retVal = remove ( outName );
      if (retVal != 0)
         fprintf ( stderr,
                   "%s: WARNING: deletion of output file (apparently) failed.\n",
                   progName );
   }
   if (numFileNames > 0 && numFilesProcessed < numFileNames) {
      fprintf ( stderr, 
                "%s: WARNING: some files have not been processed:\n"
                "\t%d specified on command line, %d not processed yet.\n\n",
                progName, numFileNames, 
                          numFileNames - numFilesProcessed );
   }
   exit ( ec );
}


/*---------------------------------------------*/
void panic ( Char* s )
{
   fprintf ( stderr,
             "\n%s: PANIC -- internal consistency error:\n"
             "\t%s\n"
             "\tThis is a BUG.  Please report it to me at:\n"
             "\tjseward@acm.org\n",
             progName, s );
   showFileNames();
   cleanUpAndFail( 3 );
}


/*---------------------------------------------*/
void badBGLengths ( void )
{
   fprintf ( stderr,
             "\n%s: error when reading background model code lengths,\n"
             "\twhich probably means the compressed file is corrupted.\n",
             progName );
   showFileNames();
   cadvise();
   cleanUpAndFail( 2 );
}


/*---------------------------------------------*/
void crcError ( UInt32 crcStored, UInt32 crcComputed )
{
   fprintf ( stderr,
             "\n%s: Data integrity error when decompressing.\n"
             "\tStored CRC = 0x%x, computed CRC = 0x%x\n",
             progName, crcStored, crcComputed );
   showFileNames();
   cadvise();
   cleanUpAndFail( 2 );
}


/*---------------------------------------------*/
void compressedStreamEOF ( void )
{
   fprintf ( stderr,
             "\n%s: Compressed file ends unexpectedly;\n\t"
             "perhaps it is corrupted?  *Possible* reason follows.\n",
             progName );
   perror ( progName );
   showFileNames();
   cadvise();
   cleanUpAndFail( 2 );
}


/*---------------------------------------------*/
void ioError ( )
{
   fprintf ( stderr,
             "\n%s: I/O or other error, bailing out.  Possible reason follows.\n",
             progName );
   perror ( progName );
   showFileNames();
   cleanUpAndFail( 1 );
}


/*---------------------------------------------*/
void blockOverrun ()
{
   fprintf ( stderr,
             "\n%s: block overrun during decompression,\n"
             "\twhich probably means the compressed file\n"
             "\tis corrupted.\n",
             progName );
   showFileNames();
   cadvise();
   cleanUpAndFail( 2 );
}


/*---------------------------------------------*/
void badBlockHeader ()
{
   fprintf ( stderr,
             "\n%s: bad block header in the compressed file,\n"
             "\twhich probably means it is corrupted.\n",
             progName );
   showFileNames();
   cadvise();
   cleanUpAndFail( 2 );
}


/*---------------------------------------------*/
void bitStreamEOF ()
{
   fprintf ( stderr,
             "\n%s: read past the end of compressed data,\n"
             "\twhich probably means it is corrupted.\n",
             progName );
   showFileNames();
   cadvise();
   cleanUpAndFail( 2 );
}


/*---------------------------------------------*/
void mySignalCatcher ( IntNative n )
{
   fprintf ( stderr,
             "\n%s: Control-C (or similar) caught, quitting.\n",
             progName );
   cleanUpAndFail(1);
}


/*---------------------------------------------*/
void mySIGSEGVorSIGBUScatcher ( IntNative n )
{
   if (opMode == OM_Z)
      fprintf ( stderr,
                "\n%s: Caught a SIGSEGV or SIGBUS whilst compressing,\n"
                "\twhich probably indicates a bug in bzip2.  Please\n"
                "\treport it to me at: jseward@acm.org\n",
                progName );
      else
      fprintf ( stderr,
                "\n%s: Caught a SIGSEGV or SIGBUS whilst decompressing,\n"
                "\twhich probably indicates that the compressed data\n"
                "\tis corrupted.\n",
                progName );

   showFileNames();
   if (opMode == OM_Z)
      cleanUpAndFail( 3 ); else
      { cadvise(); cleanUpAndFail( 2 ); }
}


/*---------------------------------------------*/
void uncompressOutOfMemory ( Int32 draw, Int32 blockSize )
{
   fprintf ( stderr,
             "\n%s: Can't allocate enough memory for decompression.\n"
             "\tRequested %d bytes for a block size of %d.\n"
             "\tTry selecting space-economic decompress (with flag -s)\n"
             "\tand failing that, find a machine with more memory.\n",
             progName, draw, blockSize );
   showFileNames();
   cleanUpAndFail(1);
}


/*---------------------------------------------*/
void compressOutOfMemory ( Int32 draw, Int32 blockSize )
{
   fprintf ( stderr,
             "\n%s: Can't allocate enough memory for compression.\n"
             "\tRequested %d bytes for a block size of %d.\n"
             "\tTry selecting a small block size (with flag -s).\n",
             progName, draw, blockSize );
   showFileNames();
   cleanUpAndFail(1);
}


/*---------------------------------------------------*/
/*--- The main driver machinery                   ---*/
/*---------------------------------------------------*/

/*---------------------------------------------*/
void pad ( Char *s )
{
   Int32 i;
   if ( (Int32)strlen(s) >= longestFileName ) return;
   for (i = 1; i <= longestFileName - (Int32)strlen(s); i++)
      fprintf ( stderr, " " );
}


/*---------------------------------------------*/
Bool fileExists ( Char* name )
{
   FILE *tmp   = fopen ( name, "rb" );
   Bool exists = (tmp != NULL);
   if (tmp != NULL) fclose ( tmp );
   return exists;
}


/*---------------------------------------------*/
/*--
  if in doubt, return True
--*/
Bool notABogStandardFile ( Char* name )
{
   IntNative      i;
   struct MY_STAT statBuf;

   i = MY_LSTAT ( name, &statBuf );
   if (i != 0) return True;
   if (MY_S_IFREG(statBuf.st_mode)) return False;
   return True;
}


/*---------------------------------------------*/
void copyDateAndPermissions ( Char *srcName, Char *dstName )
{
   #if BZ_UNIX
   IntNative      retVal;
   struct MY_STAT statBuf;
   struct utimbuf uTimBuf;

   retVal = MY_LSTAT ( srcName, &statBuf );
   ERROR_IF_NOT_ZERO ( retVal );
   uTimBuf.actime = statBuf.st_atime;
   uTimBuf.modtime = statBuf.st_mtime;

   retVal = chmod ( dstName, statBuf.st_mode );
   ERROR_IF_NOT_ZERO ( retVal );
   retVal = utime ( dstName, &uTimBuf );
   ERROR_IF_NOT_ZERO ( retVal );
   #endif
}


/*---------------------------------------------*/
Bool endsInBz2 ( Char* name )
{
   Int32 n = strlen ( name );
   if (n <= 4) return False;
   return
      (name[n-4] == '.' &&
       name[n-3] == 'b' &&
       name[n-2] == 'z' &&
       name[n-1] == '2');
}


/*---------------------------------------------*/
Bool containsDubiousChars ( Char* name )
{
   Bool cdc = False;
   for (; *name != '\0'; name++)
      if (*name == '?' || *name == '*') cdc = True;
   return cdc;
}


/*---------------------------------------------*/
void compress ( Char *name )
{
   FILE *inStr;
   FILE *outStr;

   if (name == NULL && srcMode != SM_I2O)
      panic ( "compress: bad modes\n" );

   switch (srcMode) {
      case SM_I2O: strcpy ( inName, "(stdin)" );
                   strcpy ( outName, "(stdout)" ); break;
      case SM_F2F: strcpy ( inName, name );
                   strcpy ( outName, name );
                   strcat ( outName, ".bz2" ); break;
      case SM_F2O: strcpy ( inName, name );
                   strcpy ( outName, "(stdout)" ); break;
   }

   if ( srcMode != SM_I2O && containsDubiousChars ( inName ) ) {
      fprintf ( stderr, "%s: There are no files matching `%s'.\n",
      progName, inName );
      return;
   }
   if ( srcMode != SM_I2O && !fileExists ( inName ) ) {
      fprintf ( stderr, "%s: Input file %s doesn't exist, skipping.\n",
                progName, inName );
      return;
   }
   if ( srcMode != SM_I2O && endsInBz2 ( inName )) {
      fprintf ( stderr, "%s: Input file name %s ends in `.bz2', skipping.\n",
                progName, inName );
      return;
   }
   if ( srcMode != SM_I2O && notABogStandardFile ( inName )) {
      fprintf ( stderr, "%s: Input file %s is not a normal file, skipping.\n",
                progName, inName );
      return;
   }
   if ( srcMode == SM_F2F && fileExists ( outName ) ) {
      fprintf ( stderr, "%s: Output file %s already exists, skipping.\n",
                progName, outName );
      return;
   }

   switch ( srcMode ) {

      case SM_I2O:
         inStr = stdin;
         outStr = stdout;
         if ( isatty ( fileno ( stdout ) ) ) {
            fprintf ( stderr,
                      "%s: I won't write compressed data to a terminal.\n",
                      progName );
            fprintf ( stderr, "%s: For help, type: `%s --help'.\n",
                              progName, progName );
            return;
         };
         break;

      case SM_F2O:
         inStr = fopen ( inName, "rb" );
         outStr = stdout;
         if ( isatty ( fileno ( stdout ) ) ) {
            fprintf ( stderr,
                      "%s: I won't write compressed data to a terminal.\n",
                      progName );
            fprintf ( stderr, "%s: For help, type: `%s --help'.\n",
                              progName, progName );
            return;
         };
         if ( inStr == NULL ) {
            fprintf ( stderr, "%s: Can't open input file %s, skipping.\n",
                      progName, inName );
            return;
         };
         break;

      case SM_F2F:
         inStr = fopen ( inName, "rb" );
         outStr = fopen ( outName, "wb" );
         if ( outStr == NULL) {
            fprintf ( stderr, "%s: Can't create output file %s, skipping.\n",
                      progName, outName );
            return;
         }
         if ( inStr == NULL ) {
            fprintf ( stderr, "%s: Can't open input file %s, skipping.\n",
                      progName, inName );
            return;
         };
         break;

      default:
         panic ( "compress: bad srcMode" );
         break;
   }

   if (verbosity >= 1) {
      fprintf ( stderr,  "  %s: ", inName );
      pad ( inName );
      fflush ( stderr );
   }

   /*--- Now the input and output handles are sane.  Do the Biz. ---*/
   outputHandleJustInCase = outStr;
   compressStream ( inStr, outStr );
   outputHandleJustInCase = NULL;

   /*--- If there was an I/O error, we won't get here. ---*/
   if ( srcMode == SM_F2F ) {
      copyDateAndPermissions ( inName, outName );
      if ( !keepInputFiles ) {
         IntNative retVal = remove ( inName );
         ERROR_IF_NOT_ZERO ( retVal );
      }
   }
}


/*---------------------------------------------*/
void uncompress ( Char *name )
{
   FILE *inStr;
   FILE *outStr;
   Bool magicNumberOK;

   if (name == NULL && srcMode != SM_I2O)
      panic ( "uncompress: bad modes\n" );

   switch (srcMode) {
      case SM_I2O: strcpy ( inName, "(stdin)" );
                   strcpy ( outName, "(stdout)" ); break;
      case SM_F2F: strcpy ( inName, name );
                   strcpy ( outName, name );
                   if (endsInBz2 ( outName ))
                      outName [ strlen ( outName ) - 4 ] = '\0';
                   break;
      case SM_F2O: strcpy ( inName, name );
                   strcpy ( outName, "(stdout)" ); break;
   }

   if ( srcMode != SM_I2O && containsDubiousChars ( inName ) ) {
      fprintf ( stderr, "%s: There are no files matching `%s'.\n",
                progName, inName );
      return;
   }
   if ( srcMode != SM_I2O && !fileExists ( inName ) ) {
      fprintf ( stderr, "%s: Input file %s doesn't exist, skipping.\n",
                progName, inName );
      return;
   }
   if ( srcMode != SM_I2O && !endsInBz2 ( inName )) {
      fprintf ( stderr,
                "%s: Input file name %s doesn't end in `.bz2', skipping.\n",
                progName, inName );
      return;
   }
   if ( srcMode != SM_I2O && notABogStandardFile ( inName )) {
      fprintf ( stderr, "%s: Input file %s is not a normal file, skipping.\n",
                progName, inName );
      return;
   }
   if ( srcMode == SM_F2F && fileExists ( outName ) ) {
      fprintf ( stderr, "%s: Output file %s already exists, skipping.\n",
                progName, outName );
      return;
   }

   switch ( srcMode ) {

      case SM_I2O:
         inStr = stdin;
         outStr = stdout;
         if ( isatty ( fileno ( stdin ) ) ) {
            fprintf ( stderr,
                      "%s: I won't read compressed data from a terminal.\n",
                      progName );
            fprintf ( stderr, "%s: For help, type: `%s --help'.\n",
                              progName, progName );
            return;
         };
         break;

      case SM_F2O:
         inStr = fopen ( inName, "rb" );
         outStr = stdout;
         if ( inStr == NULL ) {
            fprintf ( stderr, "%s: Can't open input file %s, skipping.\n",
                      progName, inName );
            return;
         };
         break;

      case SM_F2F:
         inStr = fopen ( inName, "rb" );
         outStr = fopen ( outName, "wb" );
         if ( outStr == NULL) {
            fprintf ( stderr, "%s: Can't create output file %s, skipping.\n",
                      progName, outName );
            return;
         }
         if ( inStr == NULL ) {
            fprintf ( stderr, "%s: Can't open input file %s, skipping.\n",
                      progName, inName );
            return;
         };
         break;

      default:
         panic ( "uncompress: bad srcMode" );
         break;
   }

   if (verbosity >= 1) {
      fprintf ( stderr, "  %s: ", inName );
      pad ( inName );
      fflush ( stderr );
   }

   /*--- Now the input and output handles are sane.  Do the Biz. ---*/
   outputHandleJustInCase = outStr;
   magicNumberOK = uncompressStream ( inStr, outStr );
   outputHandleJustInCase = NULL;

   /*--- If there was an I/O error, we won't get here. ---*/
   if ( magicNumberOK ) {
      if ( srcMode == SM_F2F ) {
         copyDateAndPermissions ( inName, outName );
         if ( !keepInputFiles ) {
            IntNative retVal = remove ( inName );
            ERROR_IF_NOT_ZERO ( retVal );
         }
      }
   } else {
      if ( srcMode == SM_F2F ) {
         IntNative retVal = remove ( outName );
         ERROR_IF_NOT_ZERO ( retVal );
      }
   }

   if ( magicNumberOK ) {
      if (verbosity >= 1)
         fprintf ( stderr, "done\n" );
   } else {
      if (verbosity >= 1)
         fprintf ( stderr, "not a bzip2 file, skipping.\n" ); else
         fprintf ( stderr,
                   "%s: %s is not a bzip2 file, skipping.\n",
                   progName, inName );
   }

}


/*---------------------------------------------*/
void testf ( Char *name )
{
   FILE *inStr;
   Bool allOK;

   if (name == NULL && srcMode != SM_I2O)
      panic ( "testf: bad modes\n" );

   strcpy ( outName, "(none)" );
   switch (srcMode) {
      case SM_I2O: strcpy ( inName, "(stdin)" ); break;
      case SM_F2F: strcpy ( inName, name ); break;
      case SM_F2O: strcpy ( inName, name ); break;
   }

   if ( srcMode != SM_I2O && containsDubiousChars ( inName ) ) {
      fprintf ( stderr, "%s: There are no files matching `%s'.\n",
                progName, inName );
      return;
   }
   if ( srcMode != SM_I2O && !fileExists ( inName ) ) {
      fprintf ( stderr, "%s: Input file %s doesn't exist, skipping.\n",
                progName, inName );
      return;
   }
   if ( srcMode != SM_I2O && !endsInBz2 ( inName )) {
      fprintf ( stderr,
                "%s: Input file name %s doesn't end in `.bz2', skipping.\n",
                progName, inName );
      return;
   }
   if ( srcMode != SM_I2O && notABogStandardFile ( inName )) {
      fprintf ( stderr, "%s: Input file %s is not a normal file, skipping.\n",
                progName, inName );
      return;
   }

   switch ( srcMode ) {

      case SM_I2O:
         if ( isatty ( fileno ( stdin ) ) ) {
            fprintf ( stderr,
                      "%s: I won't read compressed data from a terminal.\n",
                      progName );
            fprintf ( stderr, "%s: For help, type: `%s --help'.\n",
                              progName, progName );
            return;
         };
         inStr = stdin;
         break;

      case SM_F2O: case SM_F2F:
         inStr = fopen ( inName, "rb" );
         if ( inStr == NULL ) {
            fprintf ( stderr, "%s: Can't open input file %s, skipping.\n",
                      progName, inName );
            return;
         };
         break;

      default:
         panic ( "testf: bad srcMode" );
         break;
   }

   if (verbosity >= 1) {
      fprintf ( stderr, "  %s: ", inName );
      pad ( inName );
      fflush ( stderr );
   }

   /*--- Now the input handle is sane.  Do the Biz. ---*/
   allOK = testStream ( inStr );

   if (allOK && verbosity >= 1) fprintf ( stderr, "ok\n" );
   if (!allOK) testFailsExist = True;
}


/*---------------------------------------------*/
void license ( void )
{
   fprintf ( stderr,

    "bzip2, a block-sorting file compressor.  "
    "Version 0.1pl2, 29-Aug-97.\n"
    "   \n"
    "   Copyright (C) 1996, 1997 by Julian Seward.\n"
    "   \n"
    "   This program is free software; you can redistribute it and/or modify\n"
    "   it under the terms of the GNU General Public License as published by\n"
    "   the Free Software Foundation; either version 2 of the License, or\n"
    "   (at your option) any later version.\n"
    "   \n"
    "   This program is distributed in the hope that it will be useful,\n"
    "   but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
    "   GNU General Public License for more details.\n"
    "   \n"
    "   You should have received a copy of the GNU General Public License\n"
    "   along with this program; if not, write to the Free Software\n"
    "   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.\n"
    "   \n"
    "   The GNU General Public License is contained in the file LICENSE.\n"
    "   \n"
   );
}


/*---------------------------------------------*/
void usage ( Char *fullProgName )
{
   fprintf (
      stderr,
      "bzip2, a block-sorting file compressor.  "
      "Version 0.1pl2, 29-Aug-97.\n"
      "\n   usage: %s [flags and input files in any order]\n"
      "\n"
      "   -h --help           print this message\n"
      "   -d --decompress     force decompression\n"
      "   -f --compress       force compression\n"
      "   -t --test           test compressed file integrity\n"
      "   -c --stdout         output to standard out\n"
      "   -v --verbose        be verbose (a 2nd -v gives more)\n"
      "   -k --keep           keep (don't delete) input files\n"
      "   -L --license        display software version & license\n"
      "   -V --version        display software version & license\n"
      "   -s --small          use less memory (at most 2500k)\n"
      "   -1 .. -9            set block size to 100k .. 900k\n"
      "   --repetitive-fast   compress repetitive blocks faster\n"
      "   --repetitive-best   compress repetitive blocks better\n"
      "\n"
      "   If invoked as `bzip2', the default action is to compress.\n"
      "              as `bunzip2', the default action is to decompress.\n"
      "\n"
      "   If no file names are given, bzip2 compresses or decompresses\n"
      "   from standard input to standard output.  You can combine\n"
      "   flags, so `-v -4' means the same as -v4 or -4v, &c.\n"
      #if BZ_UNIX
      "\n"
      #endif
      ,

      fullProgName
   );
}


/*---------------------------------------------*/
/*--
  All the garbage from here to main() is purely to
  implement a linked list of command-line arguments,
  into which main() copies argv[1 .. argc-1].

  The purpose of this ridiculous exercise is to
  facilitate the expansion of wildcard characters
  * and ? in filenames for halfwitted OSs like
  MSDOS, Windows 95 and NT.

  The actual Dirty Work is done by the platform-specific
  macro APPEND_FILESPEC.
--*/

typedef
   struct zzzz {
      Char        *name;
      struct zzzz *link;
   }
   Cell;


/*---------------------------------------------*/
void *myMalloc ( Int32 n )
{
   void* p;

   p = malloc ( (size_t)n );
   if (p == NULL) {
      fprintf (
         stderr,
         "%s: `malloc' failed on request for %d bytes.\n",
         progName, n
      );
      exit ( 1 );
   }
   return p;
}


/*---------------------------------------------*/
Cell *mkCell ( void )
{
   Cell *c;

   c = (Cell*) myMalloc ( sizeof ( Cell ) );
   c->name = NULL;
   c->link = NULL;
   return c;
}


/*---------------------------------------------*/
Cell *snocString ( Cell *root, Char *name )
{
   if (root == NULL) {
      Cell *tmp = mkCell();
      tmp->name = (Char*) myMalloc ( 5 + strlen(name) );
      strcpy ( tmp->name, name );
      return tmp;
   } else {
      Cell *tmp = root;
      while (tmp->link != NULL) tmp = tmp->link;
      tmp->link = snocString ( tmp->link, name );
      return root;
   }
}



/*---------------------------------------------*/
#define ISFLAG(s) (strcmp(aa->name, (s))==0)


IntNative main ( IntNative argc, Char *argv[] )
{
   Int32  i, j;
   Char   *tmp;
   Cell   *argList;
   Cell   *aa;


   #if DEBUG
      fprintf ( stderr, "bzip2: *** compiled with debugging ON ***\n" );
   #endif

   /*-- Be really really really paranoid :-) --*/
   if (sizeof(Int32) != 4 || sizeof(UInt32) != 4  ||
       sizeof(Int16) != 2 || sizeof(UInt16) != 2  ||
       sizeof(Char)  != 1 || sizeof(UChar)  != 1) {
      fprintf ( stderr,
                "bzip2: I'm not configured correctly for this platform!\n"
                "\tI require Int32, Int16 and Char to have sizes\n"
                "\tof 4, 2 and 1 bytes to run properly, and they don't.\n"
                "\tProbably you can fix this by defining them correctly,\n"
                "\tand recompiling.  Bye!\n" );
      exit(1);
   }


   /*-- Set up signal handlers --*/
   signal (SIGINT,  mySignalCatcher);
   signal (SIGTERM, mySignalCatcher);
   signal (SIGSEGV, mySIGSEGVorSIGBUScatcher);
   #if BZ_UNIX
   signal (SIGHUP,  mySignalCatcher);
   signal (SIGBUS,  mySIGSEGVorSIGBUScatcher);
   #endif


   /*-- Initialise --*/
   outputHandleJustInCase  = NULL;
   ftab                    = NULL;
   ll4                     = NULL;
   ll16                    = NULL;
   ll8                     = NULL;
   tt                      = NULL;
   block                   = NULL;
   zptr                    = NULL;
   smallMode               = False;
   keepInputFiles          = False;
   verbosity               = 0;
   blockSize100k           = 9;
   testFailsExist          = False;
   bsStream                = NULL;
   numFileNames            = 0;
   numFilesProcessed       = 0;
   workFactor              = 30;

   strcpy ( inName,  "(none)" );
   strcpy ( outName, "(none)" );

   strcpy ( progNameReally, argv[0] );
   progName = &progNameReally[0];
   for (tmp = &progNameReally[0]; *tmp != '\0'; tmp++)
      if (*tmp == PATH_SEP) progName = tmp + 1;


   /*-- Expand filename wildcards in arg list --*/
   argList = NULL;
   for (i = 1; i <= argc-1; i++)
      APPEND_FILESPEC(argList, argv[i]);


   /*-- Find the length of the longest filename --*/
   longestFileName = 7;
   numFileNames    = 0;
   for (aa = argList; aa != NULL; aa = aa->link)
      if (aa->name[0] != '-') {
         numFileNames++;
         if (longestFileName < (Int32)strlen(aa->name) )
            longestFileName = (Int32)strlen(aa->name);
      }


   /*-- Determine what to do (compress/uncompress/test). --*/
   /*-- Note that subsequent flag handling may change this. --*/
   opMode = OM_Z;

   if ( (strcmp ( "bunzip2",     progName ) == 0) ||
        (strcmp ( "BUNZIP2",     progName ) == 0) ||
        (strcmp ( "bunzip2.exe", progName ) == 0) ||
        (strcmp ( "BUNZIP2.EXE", progName ) == 0) )
      opMode = OM_UNZ;


   /*-- Determine source modes; flag handling may change this too. --*/
   if (numFileNames == 0)
      srcMode = SM_I2O; else srcMode = SM_F2F;


   /*-- Look at the flags. --*/
   for (aa = argList; aa != NULL; aa = aa->link)
      if (aa->name[0] == '-' && aa->name[1] != '-')
         for (j = 1; aa->name[j] != '\0'; j++)
            switch (aa->name[j]) {
               case 'c': srcMode          = SM_F2O; break;
               case 'd': opMode           = OM_UNZ; break;
               case 'f': opMode           = OM_Z; break;
               case 't': opMode           = OM_TEST; break;
               case 'k': keepInputFiles   = True; break;
               case 's': smallMode        = True; break;
               case '1': blockSize100k    = 1; break;
               case '2': blockSize100k    = 2; break;
               case '3': blockSize100k    = 3; break;
               case '4': blockSize100k    = 4; break;
               case '5': blockSize100k    = 5; break;
               case '6': blockSize100k    = 6; break;
               case '7': blockSize100k    = 7; break;
               case '8': blockSize100k    = 8; break;
               case '9': blockSize100k    = 9; break;
               case 'V':
               case 'L': license();            break;
               case 'v': verbosity++; break;
               case 'h': usage ( progName );
                         exit ( 1 );
                         break;
               default:  fprintf ( stderr, "%s: Bad flag `%s'\n",
                                   progName, aa->name );
                         usage ( progName );
                         exit ( 1 );
                         break;
         }

   /*-- And again ... --*/
   for (aa = argList; aa != NULL; aa = aa->link) {
      if (ISFLAG("--stdout"))            srcMode          = SM_F2O;  else
      if (ISFLAG("--decompress"))        opMode           = OM_UNZ;  else
      if (ISFLAG("--compress"))          opMode           = OM_Z;    else
      if (ISFLAG("--test"))              opMode           = OM_TEST; else
      if (ISFLAG("--keep"))              keepInputFiles   = True;    else
      if (ISFLAG("--small"))             smallMode        = True;    else
      if (ISFLAG("--version"))           license();                  else
      if (ISFLAG("--license"))           license();                  else
      if (ISFLAG("--repetitive-fast"))   workFactor = 5;             else
      if (ISFLAG("--repetitive-best"))   workFactor = 150;           else
      if (ISFLAG("--verbose"))           verbosity++;                else
      if (ISFLAG("--help"))              { usage ( progName ); exit ( 1 ); }
         else
         if (strncmp ( aa->name, "--", 2) == 0) {
            fprintf ( stderr, "%s: Bad flag `%s'\n", progName, aa->name );
            usage ( progName );
            exit ( 1 );
         }
   }

   if (opMode == OM_Z && smallMode) blockSize100k = 2;

   if (opMode == OM_Z && srcMode == SM_F2O && numFileNames > 1) {
      fprintf ( stderr, "%s: I won't compress multiple files to stdout.\n",
                progName );
      exit ( 1 );
   }

   if (srcMode == SM_F2O && numFileNames == 0) {
      fprintf ( stderr, "%s: -c expects at least one filename.\n",
                progName );
      exit ( 1 );
   }

   if (opMode == OM_TEST && srcMode == SM_F2O) {
      fprintf ( stderr, "%s: -c and -t cannot be used together.\n",
                progName );
      exit ( 1 );
   }

   if (opMode != OM_Z) blockSize100k = 0;

   if (opMode == OM_Z) {
      allocateCompressStructures();
      if (srcMode == SM_I2O)
         compress ( NULL );
         else
         for (aa = argList; aa != NULL; aa = aa->link)
            if (aa->name[0] != '-') {
               numFilesProcessed++;
               compress ( aa->name );
            }
   } else
   if (opMode == OM_UNZ) {
      if (srcMode == SM_I2O)
         uncompress ( NULL );
         else
         for (aa = argList; aa != NULL; aa = aa->link)
            if (aa->name[0] != '-') {
               numFilesProcessed++;
               uncompress ( aa->name );
            }
   } else {
      testFailsExist = False;
      if (srcMode == SM_I2O)
         testf ( NULL );
         else
         for (aa = argList; aa != NULL; aa = aa->link)
            if (aa->name[0] != '-') {
               numFilesProcessed++;
               testf ( aa->name );
            }
      if (testFailsExist) {
         fprintf ( stderr,
           "\n"
           "You can use the `bzip2recover' program to *attempt* to recover\n"
           "data from undamaged sections of corrupted files.\n\n"
         );
         exit(2);
      }
   }
   return 0;
}


/*-----------------------------------------------------------*/
/*--- end                                         bzip2.c ---*/
/*-----------------------------------------------------------*/
