/*-
  decode.c -- low-level decompressor

  Copyright (C) 2011, 2012, 2013 Mikolaj Izdebski

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
#include <arpa/inet.h>          /* ntohl() */
#include <string.h>             /* memcpy() */

#include "main.h"
#include "decode.h"


/* Prefix code decoding is performed using a multilevel table lookup.
   The fastest way to decode is to simply build a lookup table whose size
   is determined by the longest code.  However, the time it takes to build
   this table can also be a factor if the data being decoded is not very
   long.  The most common codes are necessarily the shortest codes, so those
   codes dominate the decoding time, and hence the speed.  The idea is you
   can have a shorter table that decodes the shorter, more probable codes,
   and then point to subsidiary tables for the longer codes.  The time it
   costs to decode the longer codes is then traded against the time it takes
   to make longer tables.

   This result of this trade are in the constant HUFF_START_WIDTH below.
   HUFF_START_WIDTH is the number of bits the first level table can decode
   in one step.  Subsequent tables always decode one bit at time. The current
   value of HUFF_START_WIDTH was determined with a series of benchmarks.
   The optimum value may differ though from machine to machine, and possibly
   even between compilers.  Your mileage may vary.
*/
#define HUFF_START_WIDTH 10


/* Notes on prefix code decoding:

   1) Width of a tree node is defined as 2^-d, where d is depth of
      that node.  A prefix tree is said to be complete iff all leaf
      widths sum to 1.  If this sum is less (greater) than 1, we say
      the tree is incomplete (oversubscribed).  See also: Kraft's
      inequality.

      In this implementation, malformed trees (oversubscribed or
      incomplete) aren't rejected directly at creation (that's the
      moment when both bad cases are detected).  Instead, invalid
      trees cause decode error only when they are actually used to
      decode a group.

      This is nonconforming behavior -- the original bzip2, which
      serves as a reference implementation, accepts malformed trees as
      long as nonexistent codes don't appear in compressed stream.
      Neither bzip2 nor any alternative implementation I know produces
      such trees, so this behavior seems sane.

   2) When held in variables, codes are usually in left-justified
      form, meaning that they occupy consecutive most significant
      bits of the variable they are stored in, while less significant
      bits of variable are padded with zeroes.

      Such form allows for easy lexicographical comparison of codes
      using unsigned arithmetic comparison operators, without the
      need for normalization.
 */


/* Structure used for quick decoding of prefix codes. */
struct tree {
  uint16_t start[1 << HUFF_START_WIDTH];
  uint64_t base[MAX_CODE_LENGTH + 2];   /* 2 sentinels (first and last pos) */
  unsigned count[MAX_CODE_LENGTH + 1];  /* 1 sentinel (first pos) */
  uint16_t perm[MAX_ALPHA_SIZE];
};
/* start[] - decoding start point.  `k = start[c] & 0x1F' is code
   length.  If k <= HUFF_START_WIDTH then `s = start[c] >> 5' is the
   immediate symbol value.  If k > HUFF_START_WIDTH then s is
   undefined, but code starting with c is guaranteed to be at least k
   bits long.

   base[] - base codes.  For k in 1..20, base[k] is either the first
   code of length k or it is equal to base[k+1] if there are no codes
   of length k.  The other 2 elements are sentinels: base[0] is always
   zero, base[21] is plus infinity (represented as UINT64_MAX).

   count[] - cumulative code length counts.  For k in 1..20, count[k]
   is the number of symbols which codes are shorter than k bits;
   count[0] is a sentinel (always zero).

   perm[] - sorting permutation.  The rules of canonical prefix coding
   require that the source alphabet is sorted stably by ascending code
   length (the order of symbols of the same code length is preserved).
   The perm table holds the sorting permutation.
*/


#define ROW_WIDTH 16u
#define SLIDE_LENGTH 8192u
#define NUM_ROWS (256u / ROW_WIDTH)
#define CMAP_BASE (SLIDE_LENGTH - 256)

struct retriever_internal_state {
  unsigned state;               /* current state of retriever FSA */
  uint8_t selector[MAX_SELECTORS];  /* coding tree selectors */
  unsigned num_trees;           /* number of prefix trees used */
  unsigned num_selectors;       /* number of tree selectors present */
  unsigned alpha_size;          /* number of distinct prefix codes */
  uint8_t code_len[MAX_ALPHA_SIZE];
  unsigned mtf[MAX_TREES];      /* current state of inverse MTF FSA */
  struct tree tree[MAX_TREES];  /* coding trees */

  uint16_t big;                 /* big descriptor of the bitmap */
  uint16_t small;               /* small descriptor of the bitmap */
  unsigned j;                   /* general purpose index */
  unsigned t;                   /* current tree number */
  unsigned g;                   /* current group number */

  uint8_t *imtf_row[NUM_ROWS];
  uint8_t imtf_slide[SLIDE_LENGTH];
  unsigned runChar;
  unsigned run;
  unsigned shift;
};


/* FSM states from which retriever can be started or resumed. */
enum {
  S_INIT,
  S_BWT_IDX,
  S_BITMAP_BIG,
  S_BITMAP_SMALL,
  S_SELECTOR_MTF,
  S_DELTA_TAG,
  S_PREFIX,
};


/* Internal symbol values differ from that used in bzip2!
   257 - RUN-A
   258 - RUN-B
   1-255 - MTFV
   0 - EOB
*/
#define RUN_A (256+1)
#define RUN_B (256+2)
#define EOB 0
#define RUN(s) ((s) - 256)
#define IS_RUN(s) ((s) >= 256)
#define IS_EOB(s) ((s) == EOB)


/* Given a list of code lengths, make a set of tables to decode that
   set of codes.  Return value is passed in mtf array of the decoder
   state.  On success value from zero to five is passed (the tables
   are built only in this case), but also error codes ERR_INCOMPLT or
   ERR_PREFIX may be returned, which means that given code set is
   incomplete or (respectively) the code is invalid (an oversubscribed
   set of lengths).

   Because the alphabet size is always less or equal to 258 (2 RUN
   symbols, at most 255 MFV values and 1 EOB symbol) the average code
   length is strictly less than 9.  Hence the probability of decoding
   code longer than 10 bits is quite small (usually < 0.2).

   lbzip2 utilises this fact by implementing a hybrid algorithm for
   prefix decoding.  For codes of length <= 10 lbzip2 maintains a LUT
   (look-up table) that maps codes directly to corresponding symbol
   values.  Codes longer than 10 bits are not mapped by the LUT are
   decoded using cannonical prefix decoding algorithm.

   The above value of 10 bits was determined using a series of
   benchmarks.  It's not hardcoded but instead it is defined as a
   constant HUFF_START_WIDTH (see the comment above).  If on some
   system a different value works better, it can be adjusted freely.
*/
static void
make_tree(struct retriever_internal_state *rs)
{
  unsigned n;                   /* alphabet size */
  const uint8_t *L;             /* code lengths */
  uint32_t *C;                  /* code length count; C[0] is a sentinel */
  uint64_t *B;                  /* left-justified base */
  uint16_t *P;                  /* symbols sorted by code length */
  uint16_t *S;                  /* lookup table */

  unsigned k;                   /* current code length */
  unsigned s;                   /* current symbol */
  unsigned cum;
  unsigned code;
  uint64_t sofar;
  uint64_t next;
  uint64_t inc;
  uint64_t v;

  /* Initialize constants. */
  n = rs->alpha_size;
  L = rs->code_len;
  C = rs->tree[rs->t].count;
  B = rs->tree[rs->t].base;
  P = rs->tree[rs->t].perm;
  S = rs->tree[rs->t].start;

  /* Count symbol lengths. */
  for (k = 0; k <= MAX_CODE_LENGTH; k++)
    C[k] = 0;
  for (s = 0; s < n; s++) {
    k = L[s];
    C[k]++;
  }
  /* Make sure there are no zero-length codes. */
  assert(C[0] == 0);

  /* Check if Kraft's inequality is satisfied. */
  sofar = 0;
  for (k = MIN_CODE_LENGTH; k <= MAX_CODE_LENGTH; k++)
    sofar += (uint64_t)C[k] << (MAX_CODE_LENGTH - k);
  if (sofar != (1 << MAX_CODE_LENGTH)) {
    rs->mtf[rs->t] =
      sofar < (1 << MAX_CODE_LENGTH) ? ERR_INCOMPLT : ERR_PREFIX;
    return;
  }

  /* Create left-justified base table. */
  sofar = 0;
  for (k = MIN_CODE_LENGTH; k <= MAX_CODE_LENGTH; k++) {
    next = sofar + ((uint64_t)C[k] << (64 - k));
    assert(next == 0 || next >= sofar);
    B[k] = sofar;
    sofar = next;
  }
  /* Ensure that "sofar" has overflowed to zero. */
  assert(sofar == 0);

  /* The last few entries of lj-base may have overflowed to zero, so replace
     all trailing zeros with the greatest possible 64-bit value (which is
     greater than the greatest possible left-justified base).
   */
  assert(k == MAX_CODE_LENGTH + 1);
  do {
    assert(k > MIN_CODE_LENGTH);
    assert(k > MAX_CODE_LENGTH || B[k] == 0);
    B[k--] = -1;
  }
  while (C[k] == 0);

  /* Transform counts into indices (cumulative counts). */
  cum = 0;
  for (k = MIN_CODE_LENGTH; k <= MAX_CODE_LENGTH; k++) {
    uint32_t t1 = C[k];
    C[k] = cum;
    cum += t1;
  }
  assert(cum == n);

  /* Perform counting sort. */
  P[C[L[0]]++] = RUN_A;
  P[C[L[1]]++] = RUN_B;
  for (s = 2; s < n - 1; s++)
    P[C[L[s]]++] = s - 1;
  P[C[L[n - 1]]++] = EOB;

  /* Create first, complete start entries. */
  code = 0;
  inc = 1 << (HUFF_START_WIDTH - 1);
  for (k = 1; k <= HUFF_START_WIDTH; k++) {
    for (s = C[k - 1]; s < C[k]; s++) {
      uint16_t x = (P[s] << 5) | k;
      v = code;
      code += inc;
      while (v < code)
        S[v++] = x;
    }
    inc >>= 1;
  }

  /* Fill remaining, incomplete start entries. */
  assert(k == HUFF_START_WIDTH + 1);
  sofar = (uint64_t)code << (64 - HUFF_START_WIDTH);
  while (code < (1 << HUFF_START_WIDTH)) {
    while (sofar >= B[k + 1])
      k++;
    S[code] = k;
    code++;
    sofar += (uint64_t)1 << (64 - HUFF_START_WIDTH);
  }
  assert(sofar == 0);

  /* Restore cumulative counts as they were destroyed by the sorting
     phase.  The sentinel wasn't touched, so no need to restore it. */
  for (k = MAX_CODE_LENGTH; k > 0; k--) {
    C[k] = C[k - 1];
  }
  assert(C[0] == 0);

  /* Valid tables were created successfully. */
  rs->mtf[rs->t] = rs->t;
}


/* The following is a lookup table for determining the position
   of the first zero bit (starting at the most significant bit)
   in a 6-bit integer.

   0xxxxx... -> 1
   10xxxx... -> 2
   110xxx... -> 3
   1110xx... -> 4
   11110x... -> 5
   111110... -> 6
   111111... -> no zeros (marked as 7)
*/
static const uint8_t table[64] = {
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 6, 7,
};

/* Pattern L[] R[]
   0xxxxx   1   0
   100xxx   3  +1
   10100x   5  +2
   101010   6  +3
   101011   6  +1
   10110x   5   0
   101110   6  +1
   101111   6  -1
   110xxx   3  -1
   11100x   5   0
   111010   6  +1
   111011   6  -1
   11110x   5  -2
   111110   6  -1
   111111   6  -3

   The actual R[] entries are biased (3 is added).
*/
static const uint8_t L[64] = {
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  3, 3, 3, 3, 3, 3, 3, 3, 5, 5, 6, 6, 5, 5, 6, 6,
  3, 3, 3, 3, 3, 3, 3, 3, 5, 5, 6, 6, 5, 5, 6, 6,
};

static const uint8_t R[64] = {
  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
  4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 6, 4, 3, 3, 4, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 4, 2, 1, 1, 2, 0,
};


#define DECLARE unsigned w; uint64_t v; const uint32_t *next, *limit,   \
                                          *tt_limit; uint32_t *tt
#define SAVE() (bs->buff = v, bs->live = w, bs->data = next,    \
                ds->block_size = tt - ds->tt)
#define RESTORE() (v = bs->buff, w = bs->live, next = bs->data,     \
                   limit = bs->limit, tt = ds->tt + ds->block_size, \
                   tt_limit = ds->tt + MAX_BLOCK_SIZE)

/* Make sure that bit buffer v holds at least 32 bits, but no more
   than 63.

   If there buffer contains from 0 to 31 bits then an attempt to
   append next 32 bits is made.  If there is not enough input
   available then current state is saved (including FSM state, which
   is saved as s) and the function returns.

   Note that it would be wrong to put more than 63 bits (i.e. 64 bits)
   in v as a potential value of UINT64_MAX could be misrepresented as
   plus infinity.
*/
#define NEED(s)                                                 \
  {                                                             \
    if (w < 32u) {                                              \
      if (unlikely(next == limit)) {                            \
        SAVE();                                                 \
        if (bs->eof)                                            \
          return ERR_EOF;                                       \
        rs->state = (s);                                        \
        return MORE;                                            \
      case (s):                                                 \
        if (unlikely(bs->data == bs->limit)) {                  \
          assert(bs->eof);                                      \
          return ERR_EOF;                                       \
        }                                                       \
        RESTORE();                                              \
        assert (w < 32u);                                       \
      }                                                         \
      v |= (uint64_t)ntohl(*next) << (64u - (w += 32u));        \
      next++;                                                   \
    }                                                           \
  }

/* Same as NEED(), but assumes that there is at least one 32-bit word
   of input available.  */
#define NEED_FAST()                                             \
  {                                                             \
    if (w < 32u) {                                              \
      v |= (uint64_t)ntohl (*next) << (64u - (w += 32u));       \
      next++;                                                   \
    }                                                           \
  }

/* Return k most significant bits of bit buffer v.  */
#define PEEK(k) (v >> (64u - (k)))

/* Remove k most significant bits of bit buffer v.  */
#define DUMP(k) (v <<= (k), w -= (k), (void)0)

/* Remove and return k most significant bits of bit buffer v.  */
#define TAKE(x,k) ((x) = PEEK(k), DUMP(k))


/* Implementation of Sliding Lists algorithm for doing Inverse
   Move-To-Front (IMTF) transformation in O(n) space and amortized
   O(sqrt(n)) time.  The naive IMTF algorithm does the same in both
   O(n) space and time.

   IMTF could be done in O(log(n)) time using algorithms based on
   (quite) complex data structures such as self-balancing binary
   search trees, but these algorithms have quite big constant factor
   which makes them impractical for MTF of 256 items.
*/
static uint8_t
mtf_one(uint8_t **imtf_row, uint8_t *imtf_slide, uint8_t c)
{
  uint8_t *pp;

  /* We expect the index to be small, so we have a special case for that. */
  if (likely(c < ROW_WIDTH)) {
    unsigned nn = c;

    pp = imtf_row[0];
    c = pp[nn];

    /* Forgive me the ugliness of this code, but mtf_one() is executed
       frequently and needs to be fast.  An equivalent (simpler and slower)
       version is given in #else clause.
     */
#if ROW_WIDTH == 16
    switch (nn) {
    default:
      abort();
#define R(n) case n: pp[n] = pp[n-1]
      R(15); R(14); R(13); R(12); R(11); R(10); R(9);
      R(8); R(7); R(6); R(5); R(4); R(3); R(2); R(1);
#undef R
    }
#else
    while (nn > 0) {
      pp[nn] = pp[nn - 1];
      nn--;
    }
#endif
  }
  else {  /* A general case for indices >= ROW_WIDTH. */

    /* If the sliding list already reached the bottom of memory pool
       allocated for it, we need to rebuild it. */
    if (unlikely(imtf_row[0] == imtf_slide)) {
      uint8_t *kk = imtf_slide + SLIDE_LENGTH;
      uint8_t **rr = imtf_row + NUM_ROWS;

      while (rr > imtf_row) {
        uint8_t *bg = *--rr;
        uint8_t *bb = bg + ROW_WIDTH;

        assert(bg >= imtf_slide && bb <= imtf_slide + SLIDE_LENGTH);

        while (bb > bg)
          *--kk = *--bb;
        *rr = kk;
      }
    }

    {
      uint8_t **lno = imtf_row + c / ROW_WIDTH;
      uint8_t *bb = *lno;

      pp = bb + c % ROW_WIDTH;
      c = *pp;

      while (pp > bb) {
        uint8_t *tt = pp--;

        *tt = *pp;
      }

      while (lno > imtf_row) {
        uint8_t **lno1 = lno;

        pp = --(*--lno);
        **lno1 = pp[ROW_WIDTH];
      }
    }
  }

  *pp = c;
  return c;
}


int
retrieve(struct decoder_state *restrict ds, struct bitstream *bs)
{
  struct retriever_internal_state *restrict rs = ds->internal_state;

  DECLARE;
  RESTORE();

  switch (rs->state) {
  case S_INIT:
    NEED(S_BWT_IDX);
    TAKE(ds->rand, 1u);
    TAKE(ds->bwt_idx, 24u);

    /* Retrieve bitmap. */
    NEED(S_BITMAP_BIG);
    TAKE(rs->big, 16u);
    rs->small = 0;
    rs->alpha_size = 0u;
    rs->j = 0;
    do {
      if (rs->big & 0x8000) {
        TAKE(rs->small, 16u);
        NEED(S_BITMAP_SMALL);
      }
      do {
        rs->imtf_slide[CMAP_BASE + rs->alpha_size] = rs->j++;
        rs->alpha_size += rs->small >> 15;
        rs->small <<= 1;
      }
      while (rs->j & 0xF);
      rs->big <<= 1;
    }
    while (rs->j < 256u);

    if (rs->alpha_size == 0)
      return ERR_BITMAP;
    rs->alpha_size += 2u;       /* -1 MTFV, +2 RUN, +1 EOB */

    TAKE(rs->num_trees, 3u);
    if (rs->num_trees < MIN_TREES || rs->num_trees > MAX_TREES)
      return ERR_TREES;

    TAKE(rs->num_selectors, 15u);
    if (rs->num_selectors == 0)
      return ERR_GROUPS;

    /* Retrieve selector MTF values. */
    for (rs->j = 0; rs->j < rs->num_selectors; rs->j++) {
      unsigned k = table[PEEK(6u)];

      if (unlikely(k > rs->num_trees))
        return ERR_SELECTOR;
      rs->selector[rs->j] = k - 1u;
      DUMP(k);
      NEED(S_SELECTOR_MTF);
    }

    /* Retrieve decoding tables. */
    for (rs->t = 0; rs->t < rs->num_trees; rs->t++) {
      rs->j = 0u;
      TAKE(rs->code_len[0u], 5);

      while (rs->j < rs->alpha_size) {
        unsigned k = PEEK(6u);

        rs->code_len[rs->j] += R[k];
        if (unlikely(rs->code_len[rs->j] < 3 + MIN_CODE_LENGTH ||
                     rs->code_len[rs->j] > 3 + MAX_CODE_LENGTH))
          return ERR_DELTA;
        rs->code_len[rs->j] -= 3;
        k = L[k];
        if (k != 6u) {
          rs->j++;
          if (rs->j < rs->alpha_size)
            rs->code_len[rs->j] = rs->code_len[rs->j - 1u];
        }
        DUMP(k);
        NEED(S_DELTA_TAG);
      }

      make_tree(rs);
    }

    /* Initialize IMTF decoding structure. */
    {
      unsigned i;

      for (i = 0; i < NUM_ROWS; i++)
        rs->imtf_row[i] = rs->imtf_slide + CMAP_BASE + i * ROW_WIDTH;
    }

    rs->runChar = rs->imtf_row[0][0];
    rs->run = 0;
    rs->shift = 0;

    /* Initialize IBWT frequency table. */
    memset(ds->ftab, 0, sizeof(ds->ftab));

    /* Retrieve block MTF values.

       Block MTF values (MTFV) are prefix-encoded with varying trees.
       MTFVs are divided into max. 18000 groups, each group contains 50 MTFVs
       (except the last one, which can contain from 1 to 50 MTFVs).

       Each group has assigned a prefix-free codebook.  As there are up to 6
       codebooks, the group's codebook number (called selector) is a value
       from 0 to 5.  A selector of 6 or 7 means oversubscribed or incomplete
       codebook.  If such selector is encountered, decoding is aborted.
    */

    /* Bound selectors at 18001. */
    if (rs->num_selectors > 18001)
      rs->num_selectors = 18001;

    for (rs->g = 0; rs->g < rs->num_selectors; rs->g++) {
      unsigned s, x, k, i;

      /* Select the tree coding this group. */
      i = rs->selector[rs->g];
      rs->t = rs->mtf[i];
      if (unlikely(rs->t >= MAX_TREES))
        return rs->t;

      /* Update IMTF table. */
      for (; i > 0; i--)
        rs->mtf[i] = rs->mtf[i - 1];
      rs->mtf[0] = rs->t;

      /* In one coding group we can have at most 50 codes, 20 bits
         each, so the largest possible group size is 1000 bits.  If
         there are at least 1000 bits of input available then we can
         safely assume that the whole group can be decoded without
         asking for more input.

         There are two code paths.  The first one is executed when
         there is at least 1024 bits of input available (i.e. 32
         words, 32 bits each).  In this case we can apply several
         optimizations, most notably we are allowed to keep state in
         local variables and we can use NEED_FAST() - faster version
         of NEED().  The second code path is executed when there is
         not enough input to for fast decoding.
      */
      if (likely((limit - next) >= 32)) {
        struct tree *T = &rs->tree[rs->t];
        unsigned j;
        unsigned run = rs->run;
        unsigned runChar = rs->runChar;
        unsigned shift = rs->shift;

        for (j = 0; j < GROUP_SIZE; j++) {
          NEED_FAST();
          x = T->start[PEEK(HUFF_START_WIDTH)];
          k = x & 0x1F;

          if (likely(k <= HUFF_START_WIDTH)) {
            s = x >> 5;
          }
          else {
            while (v >= T->base[k + 1])
              k++;
            s = T->perm[T->count[k] + ((v - T->base[k]) >> (64 - k))];
          }

          DUMP(k);

          if (unlikely(IS_EOB(s))) {
            rs->run = run;
            rs->runChar = runChar;
            goto eob;
          }

          if (likely(IS_RUN(s) && run <= MAX_BLOCK_SIZE)) {
            run += RUN(s) << shift++;
            continue;
          }

          if (unlikely(run > tt_limit - tt)) {
            return ERR_OVERFLOW;
          }

          ds->ftab[runChar] += run;
          while (run-- > 0) {
            *tt++ = runChar;
          }

          runChar = mtf_one(rs->imtf_row, rs->imtf_slide, s);
          shift = 0;
          run = 1;
        }

        rs->run = run;
        rs->runChar = runChar;
        rs->shift = shift;
      }
      else {
        /* There are up to GROUP_SIZE codes in any group. */
        for (rs->j = 0; rs->j < GROUP_SIZE; rs->j++) {
          struct tree *T;

          NEED(S_PREFIX);
          T = &rs->tree[rs->t];
          x = T->start[PEEK(HUFF_START_WIDTH)];
          k = x & 0x1F;

          if (likely(k <= HUFF_START_WIDTH)) {
            /* Use look-up table in average case. */
            s = x >> 5;
          }
          else {
            /* Code length exceeds HUFF_START_WIDTH, use canonical
               prefix decoding algorithm instead of look-up table.  */
            while (v >= T->base[k + 1])
              k++;
            s = T->perm[T->count[k] + ((v - T->base[k]) >> (64 - k))];
          }

          DUMP(k);

          if (unlikely(IS_EOB(s))) {
          eob:
            if (unlikely(rs->run > tt_limit - tt))
              return ERR_OVERFLOW;

            ds->ftab[rs->runChar] += rs->run;
            while (rs->run--) {
              *tt++ = rs->runChar;
            }

            SAVE();

            /* Sanity-check the BWT primary index. */
            if (ds->block_size == 0)
              return ERR_EMPTY;
            if (ds->bwt_idx >= ds->block_size)
              return ERR_BWTIDX;

            free(ds->internal_state);
            ds->internal_state = NULL;
            return OK;
          }

          /* If we decoded a RLE symbol, increase run length and keep
             going.  However, we need to stop accepting RLE symbols if
             the run gets too long.  Note that rejecting further RLE
             symbols after the run has reached the length of 900k bytes
             is perfectly correct because runs longer than 900k bytes
             will cause block overflow anyways and hence stop decoding
             with an error. */
          if (likely(IS_RUN(s) && rs->run <= MAX_BLOCK_SIZE)) {
            rs->run += RUN(s) << rs->shift++;
            continue;
          }

          /* At this point we most likely have a run of one or more
             bytes.  Zero-length run is possible only at the beginning,
             once per block, so any optimization involving zero-length
             runs are pointless. */
          if (unlikely(rs->run > tt_limit - tt)) {
            return ERR_OVERFLOW;
          }

          /* Dump the run. */
          ds->ftab[rs->runChar] += rs->run;
          while (rs->run-- > 0) {
            *tt++ = rs->runChar;
          }

          rs->runChar = mtf_one(rs->imtf_row, rs->imtf_slide, s);
          rs->shift = 0;
          rs->run = 1;
        }
      }
    }

    return ERR_UNTERM;

  default:
    abort();
  }
}


/*== IBWT / IMTF ==*/

/* Block size threshold above which block randomization has any effect.
   Randomizing blocks of size <= RAND_THRESH is a no-op.
*/
#define RAND_THRESH 617u

/* A table filled with arbitrary numbers, in range 50-999, used for
   derandomizing randomized blocks.  These numbers are strictly related
   to the bzip2 file format and they are not subject to change.
*/
static const uint16_t rand_table[512] = {
  619, 720, 127, 481, 931, 816, 813, 233, 566, 247, 985, 724, 205, 454, 863,
  491, 741, 242, 949, 214, 733, 859, 335, 708, 621, 574, +73, 654, 730, 472,
  419, 436, 278, 496, 867, 210, 399, 680, 480, +51, 878, 465, 811, 169, 869,
  675, 611, 697, 867, 561, 862, 687, 507, 283, 482, 129, 807, 591, 733, 623,
  150, 238, +59, 379, 684, 877, 625, 169, 643, 105, 170, 607, 520, 932, 727,
  476, 693, 425, 174, 647, +73, 122, 335, 530, 442, 853, 695, 249, 445, 515,
  909, 545, 703, 919, 874, 474, 882, 500, 594, 612, 641, 801, 220, 162, 819,
  984, 589, 513, 495, 799, 161, 604, 958, 533, 221, 400, 386, 867, 600, 782,
  382, 596, 414, 171, 516, 375, 682, 485, 911, 276, +98, 553, 163, 354, 666,
  933, 424, 341, 533, 870, 227, 730, 475, 186, 263, 647, 537, 686, 600, 224,
  469, +68, 770, 919, 190, 373, 294, 822, 808, 206, 184, 943, 795, 384, 383,
  461, 404, 758, 839, 887, 715, +67, 618, 276, 204, 918, 873, 777, 604, 560,
  951, 160, 578, 722, +79, 804, +96, 409, 713, 940, 652, 934, 970, 447, 318,
  353, 859, 672, 112, 785, 645, 863, 803, 350, 139, +93, 354, +99, 820, 908,
  609, 772, 154, 274, 580, 184, +79, 626, 630, 742, 653, 282, 762, 623, 680,
  +81, 927, 626, 789, 125, 411, 521, 938, 300, 821, +78, 343, 175, 128, 250,
  170, 774, 972, 275, 999, 639, 495, +78, 352, 126, 857, 956, 358, 619, 580,
  124, 737, 594, 701, 612, 669, 112, 134, 694, 363, 992, 809, 743, 168, 974,
  944, 375, 748, +52, 600, 747, 642, 182, 862, +81, 344, 805, 988, 739, 511,
  655, 814, 334, 249, 515, 897, 955, 664, 981, 649, 113, 974, 459, 893, 228,
  433, 837, 553, 268, 926, 240, 102, 654, 459, +51, 686, 754, 806, 760, 493,
  403, 415, 394, 687, 700, 946, 670, 656, 610, 738, 392, 760, 799, 887, 653,
  978, 321, 576, 617, 626, 502, 894, 679, 243, 440, 680, 879, 194, 572, 640,
  724, 926, +56, 204, 700, 707, 151, 457, 449, 797, 195, 791, 558, 945, 679,
  297, +59, +87, 824, 713, 663, 412, 693, 342, 606, 134, 108, 571, 364, 631,
  212, 174, 643, 304, 329, 343, +97, 430, 751, 497, 314, 983, 374, 822, 928,
  140, 206, +73, 263, 980, 736, 876, 478, 430, 305, 170, 514, 364, 692, 829,
  +82, 855, 953, 676, 246, 369, 970, 294, 750, 807, 827, 150, 790, 288, 923,
  804, 378, 215, 828, 592, 281, 565, 555, 710, +82, 896, 831, 547, 261, 524,
  462, 293, 465, 502, +56, 661, 821, 976, 991, 658, 869, 905, 758, 745, 193,
  768, 550, 608, 933, 378, 286, 215, 979, 792, 961, +61, 688, 793, 644, 986,
  403, 106, 366, 905, 644, 372, 567, 466, 434, 645, 210, 389, 550, 919, 135,
  780, 773, 635, 389, 707, 100, 626, 958, 165, 504, 920, 176, 193, 713, 857,
  265, 203, +50, 668, 108, 645, 990, 626, 197, 510, 357, 358, 850, 858, 364,
  936, 638,
};


void
decode(struct decoder_state *ds)
{
  uint32_t i, j = 0;
  uint32_t cum;
  uint8_t uc;

  uint32_t *tt = ds->tt;

  /* Transform counts into indices (cumulative counts). */
  cum = 0;
  for (i = 0; i < 256; i++)
    ds->ftab[i] = (cum += ds->ftab[i]) - ds->ftab[i];
  assert(cum == ds->block_size);


  /* Construct the IBWT singly-linked cyclic list.  Traversing that list
     starting at primary index produces the source string.

     Each list node consists of a pointer to the next node and a character
     of the source string.  Those 2 values are packed into a single 32bit
     integer.  The character is kept in bits 0-7 and the pointer in stored
     in bits 8-27.  Bits 28-31 are unused (always clear).

     Note: Iff the source string consists of a string repeated k times
     (eg. ABABAB - the string AB is repeated k=3 times) then this algorithm
     will construct k independent (not connected), isomorphic lists.
   */
  for (i = 0u; i < ds->block_size; i++) {
    uc = tt[i];
    tt[ds->ftab[uc]] += (i << 8);
    ds->ftab[uc]++;
  }
  assert(ds->ftab[255] == ds->block_size);

  /* Derandomize the block if necessary.

     The derandomization algorithm is implemented inefficiently, but the
     assumption is that randomized blocks are unlikely to be encountered.
     Most of bzip2 implementations try to avoid randomizing blocks because
     it usually leads to decreased compression ratio.
   */
  if (unlikely(ds->rand)) {
    uint8_t *block;

    /* Allocate a temporary array to hold the block. */
    block = XNMALLOC(ds->block_size, uint8_t);

    /* Copy the IBWT linked list into the temporary array. */
    j = tt[ds->bwt_idx];
    for (i = 0; i < ds->block_size; i++) {
      j = tt[j >> 8];
      block[i] = j;
    }

    /* Derandomize the block. */
    i = 0, j = RAND_THRESH;
    while (j < ds->block_size) {
      block[j] ^= 1;
      i = (i + 1) & 0x1FF;
      j += rand_table[i];
    }

    /* Reform a linked list from the array. */
    for (i = 0; i < ds->block_size; i++)
      tt[i] = ((i + 1) << 8) + block[i];

    /* Release the temporary array. */
    free(block);
  }

  ds->rle_state = 0;
  ds->rle_crc = -1;
  ds->rle_index = ds->rand ? 0 : ds->tt[ds->bwt_idx];
  ds->rle_avail = ds->block_size;
  ds->rle_prev = 0;
  ds->rle_char = 0;
}


#define M1 0xFFFFFFFFu


/* Emit decoded block into buffer buf of size *buf_sz.  Buffer size is
   updated to reflect the remaining space left in the buffer.

   Returns OK if the block was completely emitted, MORE if more output
   space is needed to fully emit the block or ERR_RUNLEN if data error
   was detected (missing run length).
*/
int
emit(struct decoder_state *ds, void *buf, size_t *buf_sz)
{
  uint32_t p;                   /* IBWT linked list pointer */
  uint32_t a;                   /* available input bytes */
  uint32_t s;                   /* CRC checksum */
  uint8_t c;                    /* current character */
  uint8_t d;                    /* next character */
  const uint32_t *t;            /* IBWT linked list base address */
  uint8_t *b;                   /* next free byte in output buffer  */
  uint32_t m;                   /* number of free output bytes available */

  assert(ds);
  assert(buf);
  assert(buf_sz && *buf_sz > 0);

  t = ds->tt;
  b = buf;
  m = *buf_sz;

  s = ds->rle_crc;
  p = ds->rle_index;
  a = ds->rle_avail;
  c = ds->rle_char;
  d = ds->rle_prev;


  /*=== UNRLE FINITE STATE AUTOMATON ===*/
  /* There are 6 states, numbered from 0 to 5. */

  /* Excuse me, but the following is a write-only code.  It wasn't written
     for readability or maintainability, but rather for high efficiency. */
  switch (ds->rle_state) {
  default:
    abort();
  case 1:
    if (unlikely(!m--))
      break;
    s = (s << 8) ^ crc_table[(s >> 24) ^ (*b++ = c)];
    if (c != d)
      break;
    if (unlikely(!a--))
      break;
    c = p = t[p >> 8];
  case 2:
    if (unlikely(!m--)) {
      ds->rle_state = 2;
      break;
    }
    s = (s << 8) ^ crc_table[(s >> 24) ^ (*b++ = c)];
    if (c != d)
      break;
    if (unlikely(!a--))
      break;
    c = p = t[p >> 8];
  case 3:
    if (unlikely(!m--)) {
      ds->rle_state = 3;
      break;
    }
    s = (s << 8) ^ crc_table[(s >> 24) ^ (*b++ = c)];
    if (c != d)
      break;
    if (unlikely(!a--))
      return ERR_RUNLEN;
    c = p = t[p >> 8];
  case 4:
    if (unlikely(m < c)) {
      c -= m;
      while (m--)
        s = (s << 8) ^ crc_table[(s >> 24) ^ (*b++ = d)];
      ds->rle_state = 4;
      break;
    }
    m -= c;
    while (c--)
      s = (s << 8) ^ crc_table[(s >> 24) ^ (*b++ = d)];
  case 0:
    if (unlikely(!a--))
      break;
    c = p = t[p >> 8];
  case 5:
    if (unlikely(!m--)) {
      ds->rle_state = 5;
      break;
    }
    s = (s << 8) ^ crc_table[(s >> 24) ^ (*b++ = c)];
  }

  if (likely(a != M1 && m != M1)) {
    for (;;) {
      if (unlikely(!a--))
        break;
      d = c;
      c = p = t[p >> 8];
      if (unlikely(!m--)) {
        ds->rle_state = 1;
        break;
      }
      s = (s << 8) ^ crc_table[(s >> 24) ^ (*b++ = c)];
      if (likely(c != d)) {
        if (unlikely(!a--))
          break;
        d = c;
        c = p = t[p >> 8];
        if (unlikely(!m--)) {
          ds->rle_state = 1;
          break;
        }
        s = (s << 8) ^ crc_table[(s >> 24) ^ (*b++ = c)];
        if (likely(c != d)) {
          if (unlikely(!a--))
            break;
          d = c;
          c = p = t[p >> 8];
          if (unlikely(!m--)) {
            ds->rle_state = 1;
            break;
          }
          s = (s << 8) ^ crc_table[(s >> 24) ^ (*b++ = c)];
          if (likely(c != d)) {
            if (unlikely(!a--))
              break;
            d = c;
            c = p = t[p >> 8];
            if (unlikely(!m--)) {
              ds->rle_state = 1;
              break;
            }
            s = (s << 8) ^ crc_table[(s >> 24) ^ (*b++ = c)];
            if (c != d)
              continue;
          }
        }
      }
      if (unlikely(!a--))
        break;
      c = p = t[p >> 8];
      if (unlikely(!m--)) {
        ds->rle_state = 2;
        break;
      }
      s = (s << 8) ^ crc_table[(s >> 24) ^ (*b++ = c)];
      if (c != d)
        continue;
      if (unlikely(!a--))
        break;
      c = p = t[p >> 8];
      if (unlikely(!m--)) {
        ds->rle_state = 3;
        break;
      }
      s = (s << 8) ^ crc_table[(s >> 24) ^ (*b++ = c)];
      if (c != d)
        continue;
      if (unlikely(!a--))
        return ERR_RUNLEN;
      if (m < (c = p = t[p >> 8])) {
        c -= m;
        while (m--)
          s = (s << 8) ^ crc_table[(s >> 24) ^ (*b++ = d)];
        ds->rle_state = 4;
        break;
      }
      m -= c;
      while (c--)
        s = (s << 8) ^ crc_table[(s >> 24) ^ (*b++ = d)];
      if (unlikely(!a--))
        break;
      c = p = t[p >> 8];
      if (unlikely(!m--)) {
        ds->rle_state = 5;
        break;
      }
      s = (s << 8) ^ crc_table[(s >> 24) ^ (*b++ = c)];
    }
  }

  /* Exactly one of `a' and `m' is equal to M1. */
  assert((a == M1) != (m == M1));

  ds->rle_avail = a;
  if (m == M1) {
    assert(a != M1);
    ds->rle_index = p;
    ds->rle_char = c;
    ds->rle_prev = d;
    ds->rle_crc = s;
    *buf_sz = 0;
    return MORE;
  }

  assert(a == M1);
  ds->crc = s ^ M1;
  *buf_sz = m;
  return OK;
}


void
decoder_init(struct decoder_state *ds)
{
  ds->internal_state = XMALLOC(struct retriever_internal_state);
  ds->internal_state->state = S_INIT;

  ds->tt = XNMALLOC(MAX_BLOCK_SIZE, uint32_t);
  ds->block_size = 0;
}


void
decoder_free(struct decoder_state *ds)
{
  free(ds->tt);
  free(ds->internal_state);
}
