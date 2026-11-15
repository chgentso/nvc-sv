//
//  Copyright (C) 2023-2024  Nick Gasson
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include "util.h"
#include "ident.h"
#include "jit/jit-dpi.h"
#include "jit/jit-exits.h"
#include "jit/jit-priv.h"
#include "jit/jit.h"
#include "option.h"
#include "rt/assert.h"
#include "rt/crv.h"
#include "rt/sva.h"
#include "thirdparty/svrand.h"
#include "vpi/vpi-model.h"
#include "thread.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#if !defined __MINGW32__ || defined HAVE_LIBSYSTRE
#include <regex.h>
#define HAVE_POSIX_REGEX 1
#endif

#ifdef ARCH_ARM64
#define HAVE_NEON
#endif

#if defined HAVE_AVX2 || defined HAVE_SSE41
#include <x86intrin.h>
#endif

#ifdef HAVE_NEON
#include <arm_neon.h>
#endif

#if defined __GNUC__ && !defined __clang__
#pragma GCC optimize ("O2")
#endif

typedef enum {
   CPU_AVX2  = 0x1,
   CPU_SSE41 = 0x2,
   CPU_NEON  = 0x04,
} cpu_feature_t;

typedef struct {
   const char     *name;
   jit_entry_fn_t  entry;
   cpu_feature_t   feature;
   ident_t         ident;
} jit_intrinsic_t;

typedef enum {
   _U  = 0x0,
   _X  = 0x1,
   _0  = 0x2,
   _1  = 0x3,
   _Z  = 0x4,
   _W  = 0x5,
   _L  = 0x6,
   _H  = 0x7,
   _DC = 0x8
} std_ulogic_t;

#define IS_01(x) \
   _Generic((x),                                                        \
            uint64_t: (((x) & UINT64_C(0x0e0e0e0e0e0e0e0e))             \
                       == UINT64_C(0x0202020202020202)),                \
            uint8_t: ((x) & 0xe) == 0x02)

__attribute__((aligned(16)))
static const uint8_t cvt_to_x01[16] = {
   _X, _X, _0, _1, _X, _X, _0, _1, _X, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

static const uint8_t xor_table[16][16] = {
   // --------------------------------------------------
   // | U   X   0   1   Z   W   L   H   -          |   |
   // --------------------------------------------------
   {   _U, _U, _U, _U, _U, _U, _U, _U, _U   },  // | U |
   {   _U, _X, _X, _X, _X, _X, _X, _X, _X   },  // | X |
   {   _U, _X, _0, _1, _X, _X, _0, _1, _X   },  // | 0 |
   {   _U, _X, _1, _0, _X, _X, _1, _0, _X   },  // | 1 |
   {   _U, _X, _X, _X, _X, _X, _X, _X, _X   },  // | Z |
   {   _U, _X, _X, _X, _X, _X, _X, _X, _X   },  // | W |
   {   _U, _X, _0, _1, _X, _X, _0, _1, _X   },  // | L |
   {   _U, _X, _1, _0, _X, _X, _1, _0, _X   },  // | H |
   {   _U, _X, _X, _X, _X, _X, _X, _X, _X   },  // | - |
};

static const uint8_t and_table[16][16] = {
   // ---------------------------------------------------
   // |  U   X   0   1   Z   W   L   H   -          |   |
   // ---------------------------------------------------
   {    _U, _U, _0, _U, _U, _U, _0, _U, _U   },  // | U |
   {    _U, _X, _0, _X, _X, _X, _0, _X, _X   },  // | X |
   {    _0, _0, _0, _0, _0, _0, _0, _0, _0   },  // | 0 |
   {    _U, _X, _0, _1, _X, _X, _0, _1, _X   },  // | 1 |
   {    _U, _X, _0, _X, _X, _X, _0, _X, _X   },  // | Z |
   {    _U, _X, _0, _X, _X, _X, _0, _X, _X   },  // | W |
   {    _0, _0, _0, _0, _0, _0, _0, _0, _0   },  // | L |
   {    _U, _X, _0, _1, _X, _X, _0, _1, _X   },  // | H |
   {    _U, _X, _0, _X, _X, _X, _0, _X, _X   },  // | - |
};

static const uint8_t or_table[16][16] = {
   // ---------------------------------------------------
   // |  U   X   0   1   Z   W   L   H   -          |   |
   // ---------------------------------------------------
   {    _U, _U, _U, _1, _U, _U, _U, _1, _U   },  // | U |
   {    _U, _X, _X, _1, _X, _X, _X, _1, _X   },  // | X |
   {    _U, _X, _0, _1, _X, _X, _0, _1, _X   },  // | 0 |
   {    _1, _1, _1, _1, _1, _1, _1, _1, _1   },  // | 1 |
   {    _U, _X, _X, _1, _X, _X, _X, _1, _X   },  // | Z |
   {    _U, _X, _X, _1, _X, _X, _X, _1, _X   },  // | W |
   {    _U, _X, _0, _1, _X, _X, _0, _1, _X   },  // | L |
   {    _1, _1, _1, _1, _1, _1, _1, _1, _1   },  // | H |
   {    _U, _X, _X, _1, _X, _X, _X, _1, _X   },  // | - |
};

#if defined HAVE_SSE41
static const uint8_t not_table[1][16] = {
   // ---------------------------------------------------
   // |  U   X   0   1   Z   W   L   H   -          |   |
   {    _U, _X, _1, _0, _X, _X, _1, _0, _X   },
};
#endif

#if defined HAVE_SSE41 || defined HAVE_NEON

// Compressed lookup tables for vectorised intrinsics.  Note the
// vectorised intrinsics all rely on being able to read up to
// OVERRUN_MARGIN (defined in src/rt/mspace.c) bytes beyond the end of
// the input arrays.

__attribute__((aligned(16)))
static const uint8_t compress_left[16] = {
   _U,   _X,   _0,   _1,
   _X,   _X,   _0,   _1,
   _X,   0xff, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff,
};

__attribute__((aligned(16)))
static const uint8_t compress_right[16] = {
   _U << 2, _X << 2, _0 << 2, _1 << 2,
   _X << 2, _X << 2, _0 << 2, _1 << 2,
   _X << 2, 0xff,    0xff,    0xff,
   0xff,    0xff,    0xff,    0xff,
};

__attribute__((aligned(16)))
static const uint8_t small_or_table[4][4] = {
   // -----------------------------
   // |  U   X   0   1        |   |
   // -----------------------------
   {    _U, _U, _U, _1 },  // | U |
   {    _U, _X, _X, _1 },  // | X |
   {    _U, _X, _0, _1 },  // | 0 |
   {    _1, _1, _1, _1 },  // | 1 |
};

__attribute__((aligned(16)))
static const uint8_t small_and_table[4][4] = {
   // -----------------------------
   // |  U   X   0   1        |   |
   // -----------------------------
   {    _U, _U, _0, _U },  // | U |
   {    _U, _X, _0, _X },  // | X |
   {    _0, _0, _0, _0 },  // | 0 |
   {    _U, _X, _0, _1 },  // | 1 |
};

__attribute__((aligned(16)))
static const uint8_t small_xor_table[4][4] = {
   // -----------------------------
   // |  U   X   0   1        |   |
   // -----------------------------
   {    _U, _U, _U, _U },  // | U |
   {    _U, _X, _X, _X },  // | X |
   {    _U, _X, _0, _1 },  // | 0 |
   {    _U, _X, _1, _0 },  // | 1 |
};

__attribute__((aligned(16)))
static const uint8_t lane_iota[16] = {
   0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};
#endif

static const uint32_t spread_nibble[16] = {
   0x02020202, 0x03020202, 0x02030202, 0x03030202,
   0x02020302, 0x03020302, 0x02030302, 0x03030302,
   0x02020203, 0x03020203, 0x02030203, 0x03030203,
   0x02020303, 0x03020303, 0x02030303, 0x03030303,
};

#ifdef HAVE_SSE41
__attribute__((aligned(16)))
static const uint8_t reverse_lane[16] = {
   15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
};

__attribute__((aligned(16)))
static const uint8_t spread_shuffle[16] = {
   1, 1, 1, 1, 1, 1, 1, 1,
   0, 0, 0, 0, 0, 0, 0, 0
};

__attribute__((aligned(16)))
static const uint8_t spread_mask[16] = {
   0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01,
   0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01,
};
#endif

static void (*ieee_packed_add)(const uint8_t *, const uint8_t *,
                               int, int, uint8_t *);

__attribute__((always_inline))
static inline void *__tlab_alloc(tlab_t *t, size_t size, size_t align)
{
   assert(t->alloc <= t->limit);
   assert((t->alloc & (sizeof(double) - 1)) == 0);
   assert(align % sizeof(double) == 0);

   // Always allocate at least 16 bytes as the ieee_packed_add routines
   // will always write a full 128-bit vector
   const size_t alignup = ALIGN_UP(size, 16);
   const size_t base = ALIGN_UP(t->alloc, align);
   if (likely(base + alignup <= t->limit)) {
      t->alloc = base + alignup;
      return t->data + base;
   }
   else
      return mspace_alloc(t->mspace, size);
}

__attribute__((always_inline))
static inline uint32_t __tlab_mark(tlab_t *t)
{
   return t->alloc;
}

__attribute__((always_inline))
static inline void __tlab_restore(tlab_t *t, uint32_t mark)
{
   assert(mark <= t->alloc);
   t->alloc = mark;
}

#if 0
static void print_bits(const char *tag, const uint8_t *bits, size_t size)
{
   static const char map[] = "UX01ZWLH-";

   printf("%s: ", tag);
   for (int i = 0; i < size; i++)
      printf("%c", map[bits[i]]);
   printf("\n");
}
#endif

#if 0
__attribute__((target("sse4.1")))
static void print_m128i(const char *tag, __m128i vec)
{
   uint8_t bytes[16];
   memcpy(bytes, &vec, sizeof(vec));

   printf("%s:", tag);
   for (int i = 0; i < 16; i++)
      printf(" %02x", bytes[i]);
   printf("\n");
}
#endif

__attribute__((cold, noinline))
static void ieee_msg_v(jit_func_t *func, jit_anchor_t *caller,
                       vhdl_severity_t severity, const char *fmt, va_list ap)
{
   jit_anchor_t frame = {
      .caller = caller,
      .func = func
   };

   jit_thread_local_t *thread = jit_thread_local();
   thread->anchor = &frame;

   diag_t *d = diag_new(get_diag_severity(severity), NULL);
   diag_vprintf(d, fmt, ap);
   diag_show_source(d, false);

   emit_vhdl_diag(d, severity);

   thread->anchor = NULL;
}

__attribute__((cold, noinline))
static void __ieee_msg(jit_func_t *func, jit_anchor_t *caller,
                       vhdl_severity_t severity, const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);

   ieee_msg_v(func, caller, severity, fmt, ap);

   va_end(ap);
}

__attribute__((cold, noinline))
static void ieee_warn(jit_func_t *func, jit_anchor_t *caller,
                      const char *fmt, ...)
{
   if (opt_get_int(OPT_IEEE_WARNINGS) != IEEE_WARNINGS_ON)
      return;

   va_list ap;
   va_start(ap, fmt);

   ieee_msg_v(func, caller, SEVERITY_WARNING, fmt, ap);

   va_end(ap);
}

__attribute__((cold, noreturn, noinline))
static void fail_in_interpreter(jit_func_t *func, jit_anchor_t *caller,
                                jit_scalar_t *args, tlab_t *tlab)
{
   // Synopsys subprograms handle edge cases poorly: punt to the
   // interpreter to generate the error message
   func->entry = jit_interp;
   jit_interp(func, caller, args, tlab);
   fatal_trace("should not return here");
}

__attribute__((always_inline))
static inline void __invert_bits(const uint8_t *input, int size,
                                 uint8_t *result)
{
   for (int i = 0; i < size; i++) {
      assert(input[i] == _0 || input[i] == _1);
      result[i] = input[i] ^ 1;
   }
}

#ifdef HAVE_SSE41
__attribute__((target("sse4.1")))
static void std_to_x01_sse41(jit_func_t *func, jit_anchor_t *anchor,
                             jit_scalar_t *args, tlab_t *tlab)
{
   const int size = args[3].integer ^ (args[3].integer >> 63);
   const uint8_t *input = args[1].pointer;

   uint8_t *result = __tlab_alloc(tlab, ALIGN_UP(size, 16), 16);

   __m128i lookup = _mm_load_si128((const __m128i *)cvt_to_x01);

   for (int pos = 0; pos < size; pos += 16) {
      __m128i in = _mm_loadu_si128((const __m128i *)(input + pos));
      __m128i out = _mm_shuffle_epi8(lookup, in);
      _mm_store_si128((__m128i *)(result + pos), out);
   }

   args[0].pointer = result;
   args[1].integer = size - 1;
   args[2].integer = ~size;
}
#endif

static void std_to_x01(jit_func_t *func, jit_anchor_t *anchor,
                       jit_scalar_t *args, tlab_t *tlab)
{
   const int size = args[3].integer ^ (args[3].integer >> 63);
   const uint8_t *input = args[1].pointer;

   uint8_t *result = __tlab_alloc(tlab, size, 8);

   for (int i = 0; i < size; i++)
      result[i] = cvt_to_x01[input[i]];

   args[0].pointer = result;
   args[1].integer = size - 1;
   args[2].integer = ~size;
}

__attribute__((always_inline))
static inline bool __all_01(const void *vec, int size)
{
   int pos = 0;
   for (; pos + 7 < size; pos += 8) {
      const uint64_t u64 = unaligned_load(vec + pos, uint64_t);
      if (!IS_01(u64))
         return false;
   }

   for (; pos < size; pos++) {
      const uint8_t u8 = *(const uint8_t *)(vec + pos);
      if (!IS_01(u8))
         return false;
   }

   return true;
}

__attribute__((cold, noinline))
static uint8_t *ieee_to_01_slow(tlab_t *tlab, const uint8_t *input,
                                int size, uint8_t xmap)
{
   uint8_t *result = __tlab_alloc(tlab, size, 8);

   bool bad = false;
   for (int i = 0; i < size; i++) {
      const uint8_t elt = input[i];
      if (elt == _1 || elt == _H)
         result[i] = _1;
      else if (elt == _0 || elt == _L)
         result[i] = _0;
      else
         bad = true;
   }

   if (bad)
      memset(result, xmap, size);

   return result;
}

__attribute__((always_inline))
static inline uint8_t *__to_01(tlab_t *tlab, const uint8_t *input,
                               int size, uint8_t xmap)
{
   assert(size > 0);

   if (__all_01(input, size))
      return (uint8_t *)input;
   else
      return ieee_to_01_slow(tlab, input, size, xmap);
}

static void ieee_to_01(jit_func_t *func, jit_anchor_t *anchor,
                       jit_scalar_t *args, tlab_t *tlab)
{
   const int size = args[3].integer ^ (args[3].integer >> 63);
   const uint8_t *input = args[1].pointer;
   const uint8_t xmap = args[4].integer;

   if (size == 0) {
      ieee_warn(func, anchor,
                "NUMERIC_STD.TO_01: null detected, returning NAU");

      args[0].pointer = NULL;
      args[1].integer = 0;
      args[2].integer = -1;
   }
   else {
      args[0].pointer = __to_01(tlab, input, size, xmap);
      args[1].integer = size - 1;
      args[2].integer = ~size;
   }
}

__attribute__((always_inline))
static inline uint8_t *__resize_unsigned(tlab_t *tlab, const void *input,
                                         int size, int newsize)
{
   if (newsize < 1)
      return NULL;
   else if (size >= newsize)
      return (uint8_t *)input + size - newsize;
   else {
      uint8_t *result = __tlab_alloc(tlab, newsize, 8);

      const int pad = newsize - size;
      memset(result, _0, pad);
      memcpy(result + pad, input, size);

      return result;
   }
}

__attribute__((always_inline))
static inline uint8_t *__resize_signed(tlab_t *tlab, const void *input,
                                       int size, int newsize)
{
   if (newsize < 1)
      return NULL;
   else if (size == newsize)
      return (uint8_t *)input;
   else if (size == 0) {
      uint8_t *result = __tlab_alloc(tlab, newsize, 8);
      memset(result, _0, newsize);
      return result;
   }
   else {
      const int bound = MIN(size, newsize) - 1;
      assert(bound >= 0);

      uint8_t *result = __tlab_alloc(tlab, newsize, 8);
      memset(result, *(uint8_t *)input, newsize - bound);
      memcpy(result + newsize - bound, input + size - bound, bound);

      return result;
   }
}

static void ieee_resize_unsigned(jit_func_t *func, jit_anchor_t *anchor,
                                 jit_scalar_t *args, tlab_t *tlab)
{
   const int size = args[3].integer ^ (args[3].integer >> 63);
   const int newsize = args[4].integer;
   const uint8_t *input = args[1].pointer;

   args[0].pointer = __resize_unsigned(tlab, input, size, newsize);
   args[1].integer = newsize - 1;
   args[2].integer = ~newsize;
}

static void ieee_resize_signed(jit_func_t *func, jit_anchor_t *anchor,
                               jit_scalar_t *args, tlab_t *tlab)
{
   const int size = args[3].integer ^ (args[3].integer >> 63);
   const int newsize = args[4].integer;
   const uint8_t *input = args[1].pointer;

   args[0].pointer = __resize_signed(tlab, input, size, newsize);
   args[1].integer = newsize - 1;
   args[2].integer = ~newsize;
}

__attribute__((always_inline))
static inline uint8_t __pack_low_bits(const void* vec)
{
   uint64_t bits = unaligned_load(vec, uint64_t);
   bits &= UINT64_C(0x0101010101010101);
   bits *= UINT64_C(0x8040201008040201);
   return bits >> 56;
}

__attribute__((always_inline))
static inline void __spread_bits_8(void *vec, uint8_t packed)
{
   unaligned_store(vec + 0, spread_nibble[(packed >> 4) & 0xf], uint32_t);
   unaligned_store(vec + 4, spread_nibble[(packed >> 0) & 0xf], uint32_t);
}

static inline uint64_t __pack_to_u64(const uint8_t *arr, int size)
{
   uint64_t val = 0;
   int i = 0;
   for (; i + 8 <= size; i += 8)
      val = (val << 8) | __pack_low_bits(arr + i);
   for (; i < size; i++)
      val = (val << 1) | (arr[i] & 1);
   return val;
}

__attribute__((always_inline))
static inline void __unpack_from_u64(uint64_t val,
                                     uint8_t *result, int size)
{
   int pos = size;
   while (pos >= 8) {
      pos -= 8;
      __spread_bits_8(result + pos, val & 0xff);
      val >>= 8;
   }
   for (int i = pos - 1; i >= 0; i--) {
      result[i] = (val & 1) | 0x02;
      val >>= 1;
   }
}

__attribute__((always_inline))
static inline void __ieee_packed_add_scalar(const uint8_t *left,
                                            const uint8_t *right, int size,
                                            int carry, uint8_t *result)
{
   int pos = size - 8;
   for (; pos > 0; pos -= 8) {
      const unsigned lbyte = __pack_low_bits(left + pos);
      const unsigned rbyte = __pack_low_bits(right + pos);
      const unsigned sum = lbyte + rbyte + carry;

      __spread_bits_8(result + pos, sum);
      carry = !!(sum & 0x100);
   }

   for (; pos + 8 > 0; pos--) {
      const unsigned lbit = left[pos + 7] & 1;
      const unsigned rbit = right[pos + 7] & 1;
      const unsigned sum = lbit + rbit + carry;
      result[pos + 7] = (sum & 1) | 0x02;
      carry = !!(sum & 2);
   }
}

__attribute__((always_inline))
static inline void __ieee_packed_add_shifted(uint8_t *left,
                                             const uint8_t *right,
                                             int size, int shift)
{
   assert(shift >= 0 && shift < size);

   const int active = size - shift;
   (*ieee_packed_add)(left, right + shift, active, 0, left);
}

__attribute__((always_inline))
static inline void __shift_bits(const uint8_t *input, int size, int shift,
                                uint8_t *result)
{
   assert(shift >= 0 && shift < size);

   const int active = size - shift;
   memcpy(result, input + shift, active);
   memset(result + active, _0, shift);
}

__attribute__((always_inline))
static inline void __mul_unsigned(const uint8_t *left, int lsize,
                                  const uint8_t *right, int rsize,
                                  tlab_t *tlab, uint8_t *result)
{
   const int size = lsize + rsize;
   const uint8_t *adval = __resize_unsigned(tlab, right, rsize, size);

   memset(result, _0, size);

   int pos = lsize - 8;
   for (; pos >= 0; pos -= 8) {
      unsigned bits = __pack_low_bits(left + pos);
      if (bits == 0)
         continue;

      const int base_shift = lsize - pos - 8;
      while (bits != 0) {
         const int shift = base_shift + __builtin_ctz(bits);
         __ieee_packed_add_shifted(result, adval, size, shift);
         bits &= bits - 1;
      }
   }

   const int rem = pos + 8;
   if (rem > 0) {
      unsigned bits = __pack_low_bits(left) >> (8 - rem);
      const int base_shift = lsize - rem;

      while (bits != 0) {
         const int shift = base_shift + __builtin_ctz(bits);
         __ieee_packed_add_shifted(result, adval, size, shift);
         bits &= bits - 1;
      }
   }
}

__attribute__((always_inline))
static inline void __mul_signed(const uint8_t *left, int lsize,
                                const uint8_t *right, int rsize,
                                tlab_t *tlab, uint8_t *result)
{
   const int size = lsize + rsize;
   const uint8_t *adval = __resize_signed(tlab, right, rsize, size);

   memset(result, _0, size);

   int pos = lsize - 8;
   for (; pos > 0; pos -= 8) {
      unsigned bits = __pack_low_bits(left + pos);
      if (bits == 0)
         continue;

      const int base_shift = lsize - pos - 8;
      while (bits != 0) {
         const int shift = base_shift + __builtin_ctz(bits);
         __ieee_packed_add_shifted(result, adval, size, shift);
         bits &= bits - 1;
      }
   }

   const int rem = pos + 7;
   if (rem > 0) {
      unsigned bits = __pack_low_bits(left + 1) >> (8 - rem);
      const int base_shift = lsize - rem - 1;

      while (bits != 0) {
         const int shift = base_shift + __builtin_ctz(bits);
         __ieee_packed_add_shifted(result, adval, size, shift);
         bits &= bits - 1;
      }
   }

   if (left[0] == _1) {
      uint8_t *neg = __tlab_alloc(tlab, size, 8);
      __shift_bits(adval, size, lsize - 1, neg);
      __invert_bits(neg, size, neg);
      (*ieee_packed_add)(result, neg, size, 1, result);
   }
}

#ifdef HAVE_SSE41
__attribute__((target("sse4.1"), always_inline))
static inline __m128i __spread_bits_16_sse41_vec(uint16_t packed)
{
   const __m128i shuffle = _mm_load_si128((const __m128i *)spread_shuffle);
   const __m128i mask = _mm_load_si128((const __m128i *)spread_mask);
   const __m128i ones = _mm_set1_epi8(1);
   const __m128i twos = _mm_set1_epi8(2);

   __m128i bits = _mm_set1_epi16(packed);
   bits = _mm_shuffle_epi8(bits, shuffle);
   bits = _mm_and_si128(bits, mask);
   bits = _mm_cmpeq_epi8(bits, mask);
   bits = _mm_and_si128(bits, ones);
   bits = _mm_or_si128(bits, twos);

   return bits;
}

__attribute__((target("sse4.1"), always_inline))
static inline void __ieee_packed_add_sse41(const uint8_t *left,
                                           const uint8_t *right,
                                           int size, int carry,
                                           uint8_t *result)
{
   const __m128i reverse = _mm_load_si128((const __m128i *)reverse_lane);
   const __m128i ones = _mm_set1_epi8(1);

   const int prefix = size & 15;
   for (int pos = size - 16; pos >= prefix; pos -= 16) {
      __m128i lvec = _mm_loadu_si128((const __m128i *)(left + pos));
      __m128i rvec = _mm_loadu_si128((const __m128i *)(right + pos));

      lvec = _mm_shuffle_epi8(lvec, reverse);
      rvec = _mm_shuffle_epi8(rvec, reverse);

      lvec = _mm_and_si128(lvec, ones);
      rvec = _mm_and_si128(rvec, ones);

      lvec = _mm_slli_epi16(lvec, 7);
      rvec = _mm_slli_epi16(rvec, 7);

      const unsigned lbits = _mm_movemask_epi8(lvec);
      const unsigned rbits = _mm_movemask_epi8(rvec);
      const unsigned sum = lbits + rbits + carry;

      __m128i spread = __spread_bits_16_sse41_vec(sum);
      _mm_storeu_si128((__m128i *)(result + pos), spread);
      carry = !!(sum & 0x10000);
   }

   if (prefix > 0) {
      __m128i iota = _mm_load_si128((const __m128i *)lane_iota);
      __m128i mask = _mm_cmplt_epi8(iota, _mm_set1_epi8(prefix));
      __m128i lvec = _mm_loadu_si128((const __m128i *)left);
      __m128i rvec = _mm_loadu_si128((const __m128i *)right);

      lvec = _mm_shuffle_epi8(lvec, reverse);
      rvec = _mm_shuffle_epi8(rvec, reverse);

      lvec = _mm_and_si128(lvec, ones);
      rvec = _mm_and_si128(rvec, ones);

      lvec = _mm_slli_epi16(lvec, 7);
      rvec = _mm_slli_epi16(rvec, 7);

      const unsigned lbits = _mm_movemask_epi8(lvec) >> (16 - prefix);
      const unsigned rbits = _mm_movemask_epi8(rvec) >> (16 - prefix);
      const unsigned sum = lbits + rbits + carry;

      __m128i tail = __spread_bits_16_sse41_vec(sum << (16 - prefix));
      __m128i prev = _mm_loadu_si128((const __m128i *)result);
      __m128i out = _mm_blendv_epi8(prev, tail, mask);

      _mm_storeu_si128((__m128i *)result, out);
   }
}
#endif

__attribute__((always_inline))
static inline uint8_t *__to_unsigned(jit_func_t *func, jit_anchor_t *anchor,
                                     tlab_t *tlab, int64_t arg, int size)
{
   const int roundup = (size + 7) & ~7;
   uint8_t *result = __tlab_alloc(tlab, roundup, 8);

   int last = 0;
   for (int pos = roundup - 8; pos >= 0; pos -= 8, arg >>= 8)
      __spread_bits_8(result + pos, (last = (arg & 0xff)));

   const uint8_t spill_mask = ~((1 << (8 - roundup + size)) - 1);
   if (unlikely(arg != 0 || (last & spill_mask)))
      ieee_warn(func, anchor, "NUMERIC_STD.TO_UNSIGNED: vector truncated");

   return result + roundup - size;
}

__attribute__((noinline))
static void ieee_plus_unsigned(jit_func_t *func, jit_anchor_t *anchor,
                               jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[3].integer);
   const int rsize = ffi_array_length(args[6].integer);
   uint8_t *left = args[1].pointer;
   uint8_t *right = args[4].pointer;

   const int size = MAX(lsize, rsize);

   if (lsize == 0 || rsize == 0) {
      args[0].pointer = NULL;
      args[1].integer = 0;
      args[2].integer = -1;
   }
   else {
      left = __resize_unsigned(tlab, left, lsize, size);
      right = __resize_unsigned(tlab, right, rsize, size);

      left = __to_01(tlab, left, size, _X);
      right = __to_01(tlab, right, size, _X);

      if (left[0] == _X)
         args[0].pointer = left;
      else if (right[0] == _X)
         args[0].pointer = right;
      else {
         uint8_t *result = __tlab_alloc(tlab, size, 8);
         (*ieee_packed_add)(left, right, size, 0, result);
         args[0].pointer = result;
      }

      args[1].integer = size - 1;
      args[2].integer = ~size;
   }
}

__attribute__((noinline))
static void ieee_plus_unsigned_natural(jit_func_t *func, jit_anchor_t *anchor,
                                       jit_scalar_t *args, tlab_t *tlab)
{
   const int size = ffi_array_length(args[3].integer);
   uint8_t *left = args[1].pointer;
   const uint64_t right = args[4].integer;

   if (size == 0) {
      args[0].pointer = NULL;
      args[1].integer = 0;
      args[2].integer = -1;
   }
   else {
      // Must be unconditional to generate warning on truncation
      uint8_t *result = __to_unsigned(func, anchor, tlab, right, size);

      left = __to_01(tlab, left, size, _X);

      if (left[0] == _X || right == 0)
         args[0].pointer = left;
      else {
         (*ieee_packed_add)(left, result, size, 0, result);
         args[0].pointer = result;
      }

      args[1].integer = size - 1;
      args[2].integer = ~size;
   }
}

__attribute__((always_inline))
static inline void __commute_vector_scalar(jit_scalar_t *args)
{
   jit_scalar_t tmp = args[1];
   args[1] = args[2];
   args[2] = args[3];
   args[3] = args[4];
   args[4] = tmp;
}

static void ieee_plus_natural_unsigned(jit_func_t *func, jit_anchor_t *anchor,
                                       jit_scalar_t *args, tlab_t *tlab)
{
   __commute_vector_scalar(args);
   ieee_plus_unsigned_natural(func, anchor, args, tlab);
}

static void ieee_plus_signed(jit_func_t *func, jit_anchor_t *anchor,
                             jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[3].integer);
   const int rsize = ffi_array_length(args[6].integer);
   uint8_t *left = args[1].pointer;
   uint8_t *right = args[4].pointer;

   const int size = MAX(lsize, rsize);

   if (lsize == 0 || rsize == 0) {
      args[0].pointer = NULL;
      args[1].integer = 0;
      args[2].integer = -1;
   }
   else {
      left = __resize_signed(tlab, left, lsize, size);
      right = __resize_signed(tlab, right, rsize, size);

      left = __to_01(tlab, left, size, _X);
      right = __to_01(tlab, right, size, _X);

      if (left[0] == _X)
         args[0].pointer = left;
      else if (right[0] == _X)
         args[0].pointer = right;
      else {
         uint8_t *result = __tlab_alloc(tlab, size, 8);
         (*ieee_packed_add)(left, right, size, 0, result);
         args[0].pointer = result;
      }

      args[1].integer = size - 1;
      args[2].integer = ~size;
   }
}

static void ieee_minus_unsigned(jit_func_t *func, jit_anchor_t *anchor,
                                jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[3].integer);
   const int rsize = ffi_array_length(args[6].integer);
   uint8_t *left = args[1].pointer;
   uint8_t *right = args[4].pointer;

   const int size = MAX(lsize, rsize);

   if (lsize == 0 || rsize == 0) {
      args[0].pointer = NULL;
      args[1].integer = 0;
      args[2].integer = -1;
   }
   else {
      left = __resize_unsigned(tlab, left, lsize, size);
      right = __resize_unsigned(tlab, right, rsize, size);

      left = __to_01(tlab, left, size, _X);
      right = __to_01(tlab, right, size, _X);

      if (left[0] == _X)
         args[0].pointer = left;
      else if (right[0] == _X)
         args[0].pointer = right;
      else {
         uint8_t *result = __tlab_alloc(tlab, size, 8);
         __invert_bits(right, size, result);
         (*ieee_packed_add)(left, result, size, 1, result);
         args[0].pointer = result;
      }

      args[1].integer = size - 1;
      args[2].integer = ~size;
   }
}

static void ieee_minus_signed(jit_func_t *func, jit_anchor_t *anchor,
                              jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[3].integer);
   const int rsize = ffi_array_length(args[6].integer);
   uint8_t *left = args[1].pointer;
   uint8_t *right = args[4].pointer;

   const int size = MAX(lsize, rsize);

   if (lsize == 0 || rsize == 0) {
      args[0].pointer = NULL;
      args[1].integer = 0;
      args[2].integer = -1;
   }
   else {
      left = __resize_signed(tlab, left, lsize, size);
      right = __resize_signed(tlab, right, rsize, size);

      left = __to_01(tlab, left, size, _X);
      right = __to_01(tlab, right, size, _X);

      if (left[0] == _X)
         args[0].pointer = left;
      else if (right[0] == _X)
         args[0].pointer = right;
      else {
         uint8_t *result = __tlab_alloc(tlab, size, 8);
         __invert_bits(right, size, result);
         (*ieee_packed_add)(left, result, size, 1, result);
         args[0].pointer = result;
      }

      args[1].integer = size - 1;
      args[2].integer = ~size;
   }
}

static void ieee_mul_unsigned(jit_func_t *func, jit_anchor_t *anchor,
                              jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[3].integer);
   const int rsize = ffi_array_length(args[6].integer);
   uint8_t *left = args[1].pointer;
   uint8_t *right = args[4].pointer;

   const int size = lsize + rsize;

   if (lsize == 0 || rsize == 0) {
      args[0].pointer = NULL;
      args[1].integer = 0;
      args[2].integer = -1;
   }
   else {
      uint8_t *result = __tlab_alloc(tlab, size, 8);
      const uint32_t mark = __tlab_mark(tlab);

      left = __to_01(tlab, left, lsize, _X);
      right = __to_01(tlab, right, rsize, _X);

      if (left[0] == _X || right[0] == _X)
         memset(result, _X, size);
      else if (size <= 64) {
         uint64_t lval = __pack_to_u64(left, lsize);
         uint64_t rval = __pack_to_u64(right, rsize);
         __unpack_from_u64(lval * rval, result, size);
      }
      else
         __mul_unsigned(left, lsize, right, rsize, tlab, result);

      __tlab_restore(tlab, mark);

      args[0].pointer = result;
      args[1].integer = size - 1;
      args[2].integer = ~size;
   }
}

static void ieee_mul_signed(jit_func_t *func, jit_anchor_t *anchor,
                            jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[3].integer);
   const int rsize = ffi_array_length(args[6].integer);
   uint8_t *left = args[1].pointer;
   uint8_t *right = args[4].pointer;

   const int size = lsize + rsize;

   if (lsize == 0 || rsize == 0) {
      args[0].pointer = NULL;
      args[1].integer = 0;
      args[2].integer = -1;
   }
   else {
      uint8_t *result = __tlab_alloc(tlab, size, 8);
      const uint32_t mark = __tlab_mark(tlab);

      left = __to_01(tlab, left, lsize, _X);
      right = __to_01(tlab, right, rsize, _X);

      if (left[0] == _X || right[0] == _X)
         memset(result, _X, size);
      else if (size <= 64) {
         uint64_t lval = __pack_to_u64(left, lsize);
         uint64_t rval = __pack_to_u64(right, rsize);
         if (lsize < 64 && (left[0] & 1))
            lval |= ~UINT64_C(0) << lsize;
         if (rsize < 64 && (right[0] & 1))
            rval |= ~UINT64_C(0) << rsize;
         __unpack_from_u64((int64_t)lval * (int64_t)rval,
                           result, size);
      }
      else
         __mul_signed(left, lsize, right, rsize, tlab, result);

      __tlab_restore(tlab, mark);

      args[0].pointer = result;
      args[1].integer = size - 1;
      args[2].integer = ~size;
   }
}

static void ieee_divmod(jit_func_t *func, jit_anchor_t *anchor,
                        jit_scalar_t *args, tlab_t *tlab)
{
   uint8_t *num = args[2].pointer;
   const int num_size = ffi_array_length(args[4].integer);
   uint8_t *denom = args[5].pointer;
   const int denom_size = ffi_array_length(args[7].integer);
   uint8_t *xquot = args[8].pointer;
   uint8_t *xremain = args[11].pointer;

   assert(ffi_array_length(args[10].integer) == num_size);
   assert(ffi_array_length(args[13].integer) == denom_size);

   const uint32_t mark = __tlab_mark(tlab);

   uint8_t *temp = __tlab_alloc(tlab, num_size + 1, 16);
   temp[0] = _0;
   memcpy(temp + 1, num, num_size);

   const int quot_size = MAX(num_size, denom_size);
   uint8_t *quot = __tlab_alloc(tlab, quot_size, 16);
   memset(quot, _0, quot_size);

   int topbit = -1;
   for (int j = denom_size - 1; j >= 0; j--) {
      if (denom[denom_size - 1 - j] == _1) {
         topbit = j;
         break;
      }
   }

   if (unlikely(topbit < 0))
      __ieee_msg(func, anchor, SEVERITY_ERROR,
                 "NUMERIC_STD.DIVMOD: DIV, MOD, or REM by zero");
   else {
      uint8_t *denom2 = __tlab_alloc(tlab, topbit + 2, 16);
      denom2[0] = _0;
      memcpy(denom2 + 1, denom + denom_size - 1 - topbit , topbit + 1);

      uint8_t *denom3 = __tlab_alloc(tlab, topbit + 2, 16);
      __invert_bits(denom2, topbit + 2, denom3);

      for (int j = num_size - (topbit + 1); j >= 0; j--) {
         uint8_t *slice = temp + num_size - (topbit + j + 1);

         int pos = 0;
         for (; pos < topbit + 1 && slice[pos] == denom2[pos]; pos++);

         if (slice[pos] >= denom2[pos]) {
            (*ieee_packed_add)(slice, denom3, topbit + 2, 1, slice);
            quot[quot_size - 1 - j] = _1;
         }
      }
   }

   memcpy(xquot, quot + quot_size - num_size, num_size);

   if (denom_size > num_size + 1) {
      memset(xremain, _0, denom_size - (num_size + 1));
      memcpy(xremain + denom_size - (num_size + 1), temp, num_size + 1);
   }
   else
      memcpy(xremain, temp + num_size + 1 - denom_size, denom_size);

   __tlab_restore(tlab, mark);
}

static bool ieee_unsigned_cmp(jit_func_t *func, jit_anchor_t *anchor,
                              jit_scalar_t *args, tlab_t *tlab,
                              uint8_t *lbyte, uint8_t *rbyte, const char *op)
{
   const int lsize = ffi_array_length(args[3].integer);
   const int rsize = ffi_array_length(args[6].integer);
   uint8_t *left = args[1].pointer;
   uint8_t *right = args[4].pointer;

   args[0].integer = 0;   // Return FALSE by default

   if (lsize < 1 || rsize < 1) {
      ieee_warn(func, anchor, "NUMERIC_STD.\"%s\": null argument detected, "
                "returning FALSE", op);
      return false;
   }

   const uint32_t mark = __tlab_mark(tlab);

   left = __to_01(tlab, left, lsize, _X);
   right = __to_01(tlab, right, rsize, _X);

   if (left[0] == _X || right[0] == _X) {
      ieee_warn(func, anchor, "NUMERIC_STD.\"%s\": metavalue detected, "
                "returning FALSE", op);
      __tlab_restore(tlab, mark);
      return false;
   }

   const int size = MAX(lsize, rsize);
   left = __resize_unsigned(tlab, left, lsize, size);
   right = __resize_unsigned(tlab, right, rsize, size);

   int pos = 0;
   for (; pos < size - 1 && left[pos] == right[pos]; pos++);

   *lbyte = left[pos];
   *rbyte = right[pos];

   __tlab_restore(tlab, mark);
   return true;
}

static void ieee_less_unsigned(jit_func_t *func, jit_anchor_t *anchor,
                               jit_scalar_t *args, tlab_t *tlab)
{
   uint8_t left, right;
   if (!ieee_unsigned_cmp(func, anchor, args, tlab, &left, &right, "<"))
      return;

   args[0].integer = left < right;
}

static void ieee_greater_unsigned(jit_func_t *func, jit_anchor_t *anchor,
                                  jit_scalar_t *args, tlab_t *tlab)
{
   uint8_t left, right;
   if (!ieee_unsigned_cmp(func, anchor, args, tlab, &left, &right, ">"))
      return;

   args[0].integer = left > right;
}

static void ieee_geq_unsigned(jit_func_t *func, jit_anchor_t *anchor,
                              jit_scalar_t *args, tlab_t *tlab)
{
   uint8_t left, right;
   if (!ieee_unsigned_cmp(func, anchor, args, tlab, &left, &right, ">="))
      return;

   args[0].integer = left >= right;
}

static void ieee_leq_unsigned(jit_func_t *func, jit_anchor_t *anchor,
                              jit_scalar_t *args, tlab_t *tlab)
{
   uint8_t left, right;
   if (!ieee_unsigned_cmp(func, anchor, args, tlab, &left, &right, "<="))
      return;

   args[0].integer = left <= right;
}

static void ieee_to_integer_unsigned(jit_func_t *func, jit_anchor_t *anchor,
                                     jit_scalar_t *args, tlab_t *tlab)
{
   const int size = ffi_array_length(args[3].integer);
   uint8_t *arg = args[1].pointer;

   if (size < 1) {
      ieee_warn(func, anchor, "NUMERIC_STD.TO_INTEGER: "
                "null detected, returning 0");
      args[0].integer = 0;
      return;
   }

   const uint32_t mark = __tlab_mark(tlab);

   arg = __to_01(tlab, arg, size, _X);

   if (arg[0] == _X) {
      ieee_warn(func, anchor, "NUMERIC_STD.TO_INTEGER: "
                "metavalue detected, returning 0");
      __tlab_restore(tlab, mark);
      args[0].integer = 0;
      return;
   }

   const uint64_t val = __pack_to_u64(arg, size);

   if (size > 31) {
      // Determine if the result overflows in which case we re-execute
      // in the interpreter to generate the correct error

      if (size > 63) {
         for (int i = 0; i < size - 63; i++) {
            if (arg[i] != _0)
               fail_in_interpreter(func, anchor, args, tlab);
         }
      }

      if ((val & UINT64_C(0xffffffff80000000)) && standard() < STD_19)
         fail_in_interpreter(func, anchor, args, tlab);
   }

   args[0].integer = val;
   __tlab_restore(tlab, mark);
}

static void ieee_to_integer_signed(jit_func_t *func, jit_anchor_t *anchor,
                                   jit_scalar_t *args, tlab_t *tlab)
{
   const int size = ffi_array_length(args[3].integer);
   uint8_t *arg = args[1].pointer;
   const int intsize = standard() < STD_19 ? 32 : 64;

   if (size < 1) {
      ieee_warn(func, anchor, "NUMERIC_STD.TO_INTEGER: "
                "null detected, returning 0");
      args[0].integer = 0;
      return;
   }

   const uint32_t mark = __tlab_mark(tlab);

   arg = __to_01(tlab, arg, size, _X);

   if (arg[0] == _X) {
      ieee_warn(func, anchor, "NUMERIC_STD.TO_INTEGER: "
                "metavalue detected, returning 0");
      __tlab_restore(tlab, mark);
      args[0].integer = 0;
      return;
   }

   if (size > intsize) {
      // Determine if the result overflows in which case we re-execute
      // in the interpreter to generate the correct error
      for (int i = 1; i <= size - intsize; i++) {
         if (arg[i] != arg[0])
            fail_in_interpreter(func, anchor, args, tlab);
      }
   }

   int64_t result;
   if (arg[0] == _0)
      result = (int64_t)__pack_to_u64(arg, size);
   else {
      uint8_t *mag = __tlab_alloc(tlab, size, 16);
      __invert_bits(arg, size, mag);
      result = -(int64_t)__pack_to_u64(mag, size) - 1;
   }

   args[0].integer = result;
   __tlab_restore(tlab, mark);
}

#ifdef HAVE_SSE41
__attribute__((target("sse4.1")))
static void ieee_and_vector_sse41(jit_func_t *func, jit_anchor_t *anchor,
                                  jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[3].integer);
   const int rsize = ffi_array_length(args[6].integer);
   uint8_t *left = args[1].pointer;
   uint8_t *right = args[4].pointer;

   if (unlikely(lsize != rsize))
      __ieee_msg(func, anchor, SEVERITY_FAILURE,
                 "STD_LOGIC_1164.\"and\": arguments of overloaded 'and' "
                 "operator are not of the same length");
   else {
      uint8_t *result = __tlab_alloc(tlab, ALIGN_UP(lsize, 16), 16);

      __m128i left_tbl  = _mm_load_si128((const __m128i *)compress_left);
      __m128i right_tbl = _mm_load_si128((const __m128i *)compress_right);
      __m128i and_tbl   = _mm_load_si128((const __m128i *)small_and_table);

      for (int pos = 0; pos < lsize; pos += 16) {
         __m128i left1  = _mm_loadu_si128((const __m128i *)(left + pos));
         __m128i right1 = _mm_loadu_si128((const __m128i *)(right + pos));
         __m128i left2  = _mm_shuffle_epi8(left_tbl, left1);
         __m128i right2 = _mm_shuffle_epi8(right_tbl, right1);
         __m128i comb   = _mm_or_si128(left2, right2);
         __m128i and    = _mm_shuffle_epi8(and_tbl, comb);
         _mm_store_si128((__m128i *)(result + pos), and);
      }

      args[0].pointer = result;
      args[1].integer = 1;
      args[2].integer = lsize;
   }
}
#endif

#ifdef HAVE_NEON
static void ieee_and_vector_neon(jit_func_t *func, jit_anchor_t *anchor,
                                 jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[3].integer);
   const int rsize = ffi_array_length(args[6].integer);
   uint8_t *left = args[1].pointer;
   uint8_t *right = args[4].pointer;

   if (unlikely(lsize != rsize))
      __ieee_msg(func, anchor, SEVERITY_FAILURE,
                 "STD_LOGIC_1164.\"and\": arguments of overloaded 'and' "
                 "operator are not of the same length");
   else {
      uint8_t *result = __tlab_alloc(tlab, ALIGN_UP(lsize, 16), 16);

      uint8x16_t left_tbl  = vld1q_u8(compress_left);
      uint8x16_t right_tbl = vld1q_u8(compress_right);
      uint8x16_t and_tbl   = vld1q_u8((const uint8_t *)small_and_table);

      for (int pos = 0; pos < lsize; pos += 16) {
         uint8x16_t left1  = vld1q_u8(left + pos);
         uint8x16_t right1 = vld1q_u8(right + pos);
         uint8x16_t left2  = vqtbl1q_u8(left_tbl, left1);
         uint8x16_t right2 = vqtbl1q_u8(right_tbl, right1);
         uint8x16_t comb   = vorrq_u8(left2, right2);
         uint8x16_t and    = vqtbl1q_u8(and_tbl, comb);
         vst1q_u8(result + pos, and);
      }

      args[0].pointer = result;
      args[1].integer = 1;
      args[2].integer = lsize;
   }
}
#endif

static void ieee_and_vector(jit_func_t *func, jit_anchor_t *anchor,
                            jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[3].integer);
   const int rsize = ffi_array_length(args[6].integer);
   uint8_t *left = args[1].pointer;
   uint8_t *right = args[4].pointer;

   if (unlikely(lsize != rsize))
      __ieee_msg(func, anchor, SEVERITY_FAILURE,
                 "STD_LOGIC_1164.\"and\": arguments of overloaded 'and' "
                 "operator are not of the same length");
   else {
      uint8_t *result = __tlab_alloc(tlab, lsize, 8);

      for (int pos = 0; pos < lsize; pos++)
         result[pos] = and_table[left[pos]][right[pos]];

      args[0].pointer = result;
      args[1].integer = 1;
      args[2].integer = lsize;
   }
}

#ifdef HAVE_SSE41
__attribute__((target("sse4.1")))
static void ieee_or_vector_sse41(jit_func_t *func, jit_anchor_t *anchor,
                                 jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[3].integer);
   const int rsize = ffi_array_length(args[6].integer);
   uint8_t *left = args[1].pointer;
   uint8_t *right = args[4].pointer;

   if (unlikely(lsize != rsize))
      __ieee_msg(func, anchor, SEVERITY_FAILURE,
                 "STD_LOGIC_1164.\"or\": arguments of overloaded 'or' "
                 "operator are not of the same length");
   else {
      uint8_t *result = __tlab_alloc(tlab, ALIGN_UP(lsize, 16), 16);

      __m128i left_tbl  = _mm_load_si128((const __m128i *)compress_left);
      __m128i right_tbl = _mm_load_si128((const __m128i *)compress_right);
      __m128i or_tbl    = _mm_load_si128((const __m128i *)small_or_table);

      for (int pos = 0; pos < lsize; pos += 16) {
         __m128i left1  = _mm_loadu_si128((const __m128i *)(left + pos));
         __m128i right1 = _mm_loadu_si128((const __m128i *)(right + pos));
         __m128i left2  = _mm_shuffle_epi8(left_tbl, left1);
         __m128i right2 = _mm_shuffle_epi8(right_tbl, right1);
         __m128i comb   = _mm_or_si128(left2, right2);
         __m128i orr    = _mm_shuffle_epi8(or_tbl, comb);
         _mm_store_si128((__m128i *)(result + pos), orr);
      }

      args[0].pointer = result;
      args[1].integer = 1;
      args[2].integer = lsize;
   }
}
#endif

#ifdef HAVE_NEON
static void ieee_or_vector_neon(jit_func_t *func, jit_anchor_t *anchor,
                                jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[3].integer);
   const int rsize = ffi_array_length(args[6].integer);
   uint8_t *left = args[1].pointer;
   uint8_t *right = args[4].pointer;

   if (unlikely(lsize != rsize))
      __ieee_msg(func, anchor, SEVERITY_FAILURE,
                 "STD_LOGIC_1164.\"or\": arguments of overloaded 'or' "
                 "operator are not of the same length");
   else {
      uint8_t *result = __tlab_alloc(tlab, ALIGN_UP(lsize, 16), 16);

      uint8x16_t left_tbl  = vld1q_u8(compress_left);
      uint8x16_t right_tbl = vld1q_u8(compress_right);
      uint8x16_t or_tbl    = vld1q_u8((const uint8_t *)small_or_table);

      for (int pos = 0; pos < lsize; pos += 16) {
         uint8x16_t left1  = vld1q_u8(left + pos);
         uint8x16_t right1 = vld1q_u8(right + pos);
         uint8x16_t left2  = vqtbl1q_u8(left_tbl, left1);
         uint8x16_t right2 = vqtbl1q_u8(right_tbl, right1);
         uint8x16_t comb   = vorrq_u8(left2, right2);
         uint8x16_t orr    = vqtbl1q_u8(or_tbl, comb);
         vst1q_u8(result + pos, orr);
      }

      args[0].pointer = result;
      args[1].integer = 1;
      args[2].integer = lsize;
   }
}
#endif

static void ieee_or_vector(jit_func_t *func, jit_anchor_t *anchor,
                                jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[3].integer);
   const int rsize = ffi_array_length(args[6].integer);
   uint8_t *left = args[1].pointer;
   uint8_t *right = args[4].pointer;

   if (unlikely(lsize != rsize))
      __ieee_msg(func, anchor, SEVERITY_FAILURE,
                 "STD_LOGIC_1164.\"or\": arguments of overloaded 'or' "
                 "operator are not of the same length");
   else {
      uint8_t *result = __tlab_alloc(tlab, ALIGN_UP(lsize, 16), 16);

      for (int pos = 0; pos < lsize; pos++)
         result[pos] = or_table[left[pos]][right[pos]];

      args[0].pointer = result;
      args[1].integer = 1;
      args[2].integer = lsize;
   }
}

#ifdef HAVE_SSE41
__attribute__((target("sse4.1")))
static void ieee_xor_vector_sse41(jit_func_t *func, jit_anchor_t *anchor,
                                  jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[3].integer);
   const int rsize = ffi_array_length(args[6].integer);
   uint8_t *left = args[1].pointer;
   uint8_t *right = args[4].pointer;

   if (unlikely(lsize != rsize))
      __ieee_msg(func, anchor, SEVERITY_FAILURE,
                 "STD_LOGIC_1164.\"xor\": arguments of overloaded 'xor' "
                 "operator are not of the same length");
   else {
      uint8_t *result = __tlab_alloc(tlab, ALIGN_UP(lsize, 16), 16);

      __m128i left_tbl  = _mm_load_si128((const __m128i *)compress_left);
      __m128i right_tbl = _mm_load_si128((const __m128i *)compress_right);
      __m128i xor_tbl   = _mm_load_si128((const __m128i *)small_xor_table);

      for (int pos = 0; pos < lsize; pos += 16) {
         __m128i left1  = _mm_loadu_si128((const __m128i *)(left + pos));
         __m128i right1 = _mm_loadu_si128((const __m128i *)(right + pos));
         __m128i left2  = _mm_shuffle_epi8(left_tbl, left1);
         __m128i right2 = _mm_shuffle_epi8(right_tbl, right1);
         __m128i comb   = _mm_or_si128(left2, right2);
         __m128i xor    = _mm_shuffle_epi8(xor_tbl, comb);
         _mm_store_si128((__m128i *)(result + pos), xor);
      }

      args[0].pointer = result;
      args[1].integer = 1;
      args[2].integer = lsize;
   }
}
#endif

#ifdef HAVE_NEON
static void ieee_xor_vector_neon(jit_func_t *func, jit_anchor_t *anchor,
                                 jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[3].integer);
   const int rsize = ffi_array_length(args[6].integer);
   uint8_t *left = args[1].pointer;
   uint8_t *right = args[4].pointer;

   if (unlikely(lsize != rsize))
      __ieee_msg(func, anchor, SEVERITY_FAILURE,
                 "STD_LOGIC_1164.\"xor\": arguments of overloaded 'xor' "
                 "operator are not of the same length");
   else {
      uint8_t *result = __tlab_alloc(tlab, ALIGN_UP(lsize, 16), 16);

      uint8x16_t left_tbl  = vld1q_u8(compress_left);
      uint8x16_t right_tbl = vld1q_u8(compress_right);
      uint8x16_t xor_tbl   = vld1q_u8((const uint8_t *)small_xor_table);

      for (int pos = 0; pos < lsize; pos += 16) {
         uint8x16_t left1  = vld1q_u8(left + pos);
         uint8x16_t right1 = vld1q_u8(right + pos);
         uint8x16_t left2  = vqtbl1q_u8(left_tbl, left1);
         uint8x16_t right2 = vqtbl1q_u8(right_tbl, right1);
         uint8x16_t comb   = vorrq_u8(left2, right2);
         uint8x16_t xor    = vqtbl1q_u8(xor_tbl, comb);
         vst1q_u8(result + pos, xor);
      }

      args[0].pointer = result;
      args[1].integer = 1;
      args[2].integer = lsize;
   }
}
#endif

static void std_xor_vector(jit_func_t *func, jit_anchor_t *anchor,
                           jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[3].integer);
   const int rsize = ffi_array_length(args[6].integer);
   uint8_t *left = args[1].pointer;
   uint8_t *right = args[4].pointer;

   if (unlikely(lsize != rsize))
      __ieee_msg(func, anchor, SEVERITY_FAILURE,
                 "STD_LOGIC_1164.\"xor\": arguments of overloaded 'xor' "
                 "operator are not of the same length");
   else {
      uint8_t *result = __tlab_alloc(tlab, lsize, 8);

      for (int pos = 0; pos < lsize; pos++)
         result[pos] = xor_table[left[pos]][right[pos]];

      args[0].pointer = result;
      args[1].integer = 1;
      args[2].integer = lsize;
   }
}

#ifdef HAVE_SSE41
__attribute__((target("sse4.1")))
static void ieee_not_vector_sse41(jit_func_t *func, jit_anchor_t *anchor,
                                  jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[3].integer);
   uint8_t *left = args[1].pointer;

   uint8_t *result = __tlab_alloc(tlab, ALIGN_UP(lsize, 16), 16);

   __m128i not_tbl = _mm_load_si128((const __m128i *)not_table);

   for (int pos = 0; pos < lsize; pos += 16) {
      __m128i input  = _mm_loadu_si128((const __m128i *)(left + pos));
      __m128i output = _mm_shuffle_epi8(not_tbl, input);
      _mm_store_si128((__m128i *)(result + pos), output);
   }

   args[0].pointer = result;
   args[1].integer = 1;
   args[2].integer = lsize;
}
#endif

static void ieee_to_unsigned(jit_func_t *func, jit_anchor_t *anchor,
                             jit_scalar_t *args, tlab_t *tlab)
{
   const int64_t size = args[2].integer;
   uint64_t arg = args[1].integer;

   if (size < 1) {
      args[0].pointer = NULL;
      args[1].integer = 0;
      args[2].integer = -1;
   }
   else {
      args[0].pointer = __to_unsigned(func, anchor, tlab, arg, size);
      args[1].integer = size - 1;
      args[2].integer = ~size;
   }
}

static void ieee_to_signed(jit_func_t *func, jit_anchor_t *anchor,
                           jit_scalar_t *args, tlab_t *tlab)
{
   const int64_t size = args[2].integer;
   int64_t arg = args[1].integer;

   if (size < 1) {
      args[0].pointer = NULL;
      args[1].integer = 0;
      args[2].integer = -1;
   }
   else {
      const int roundup = (size + 7) & ~7;
      uint8_t *result = __tlab_alloc(tlab, roundup, 8), last = 0;
      for (int pos = roundup - 8; pos >= 0; pos -= 8, arg >>= 8)
         __spread_bits_8(result + pos, (last = (arg & 0xff)));

      const uint8_t spill_mask = ~((1 << (8 - roundup + size)) - 1);
      if (unlikely((arg != 0 && arg != -1)
                   || (arg == 0 && result[roundup - size] == _1)
                   || (arg == -1 && result[roundup - size] == _0)
                   || (arg == 0 && (last & spill_mask) != 0)
                   || (arg == -1 && (last & spill_mask) != spill_mask)))
         ieee_warn(func, anchor, "NUMERIC_STD.TO_SIGNED: vector truncated");

      args[0].pointer = result + roundup - size;
      args[1].integer = size - 1;
      args[2].integer = ~size;
   }
}

__attribute__((always_inline))
static inline bool __is_x(uint8_t arg)
{
   return arg == _U || arg == _X || arg == _Z || arg == _W || arg == _DC;
}

__attribute__((always_inline))
static inline void __make_binary(jit_func_t *func, jit_anchor_t *anchor,
                                 tlab_t *tlab, const uint8_t *input,
                                 uint8_t *result, int size)
{
   static const uint8_t tbl_BINARY[] = { _X, _X, _0, _1, _X, _X, _0, _1, _X };

   for (int i = 0; i < size; i++) {
      if (unlikely(__is_x(input[i]))) {
         ieee_warn(func, anchor, "There is an 'U'|'X'|'W'|'Z'|'-' in an "
                     "arithmetic operand, the result will be 'X'(es).");
         memset(result, _X, size);
         break;
      }

      result[i] = tbl_BINARY[input[i]];
   }
}

__attribute__((always_inline))
static inline const uint8_t *__conv_unsigned(jit_func_t *func,
                                             jit_anchor_t *anchor,
                                             tlab_t *tlab,
                                             const void *input,
                                             int oldsize, int size)
{
   assert(size > 0);

   if (oldsize == size && __all_01(input, oldsize))
      return input;

   const int maxsize = MAX(oldsize, size);
   const int pad = size - oldsize;
   uint8_t *result = __tlab_alloc(tlab, maxsize, 8);

   __make_binary(func, anchor, tlab, input, result + pad, oldsize);

   if (unlikely(result[pad] == _X))
      memset(result, _X, size);
   else
      memset(result, _0, pad);

   return result;
}

__attribute__((always_inline))
static inline const uint8_t *__conv_signed(jit_func_t *func,
                                           jit_anchor_t *anchor,
                                           tlab_t *tlab,
                                           const void *input,
                                           int oldsize, int size)
{
   assert(size > 0);

   if (oldsize == size && __all_01(input, oldsize))
      return input;

   const int maxsize = MAX(oldsize, size);
   const int pad = size - oldsize;
   uint8_t *result = __tlab_alloc(tlab, maxsize, 8);

   __make_binary(func, anchor, tlab, input, result + pad, oldsize);

   if (unlikely(result[pad] == _X))
      memset(result, _X, size);
   else
      memset(result, *(uint8_t *)input, pad);

   return result;
}

static void synopsys_plus_unsigned(jit_func_t *func, jit_anchor_t *anchor,
                                   jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[3].integer);
   const int rsize = ffi_array_length(args[6].integer);
   const uint8_t *left = args[1].pointer;
   const uint8_t *right = args[4].pointer;

   const int length = MAX(lsize, rsize);
   uint8_t *result = __tlab_alloc(tlab, length, 8);

   if (unlikely(length == 0))
      fail_in_interpreter(func, anchor, args, tlab);

   left = __conv_unsigned(func, anchor, tlab, left, lsize, length);
   right = __conv_unsigned(func, anchor, tlab, right, rsize, length);

   if (unlikely(left[0] == _X || right[0] == _X))
      memset(result, _X, length);
   else
      (*ieee_packed_add)(left, right, length, 0, result);

   args[0].pointer = result;
   args[1].integer = length - 1;
   args[2].integer = ~length;
}

static void synopsys_plus_signed(jit_func_t *func, jit_anchor_t *anchor,
                                 jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[3].integer);
   const int rsize = ffi_array_length(args[6].integer);
   const uint8_t *left = args[1].pointer;
   const uint8_t *right = args[4].pointer;

   const int length = MAX(lsize, rsize);
   uint8_t *result = __tlab_alloc(tlab, length, 8);

   if (unlikely(length == 0))
      fail_in_interpreter(func, anchor, args, tlab);

   left = __conv_signed(func, anchor, tlab, left, lsize, length);
   right = __conv_signed(func, anchor, tlab, right, rsize, length);

   if (unlikely(left[0] == _X || right[0] == _X))
      memset(result, _X, length);
   else
      (*ieee_packed_add)(left, right, length, 0, result);

   args[0].pointer = result;
   args[1].integer = length - 1;
   args[2].integer = ~length;
}

static void synopsys_plus_unsigned_logic(jit_func_t *func, jit_anchor_t *anchor,
                                         jit_scalar_t *args, tlab_t *tlab)
{
   uint8_t right[] = { args[4].integer };
   args[4].pointer = right;
   args[5].integer = 0;
   args[6].integer = 1;

   synopsys_plus_unsigned(func, anchor, args, tlab);
}

static void synopsys_plus_logic_unsigned(jit_func_t *func, jit_anchor_t *anchor,
                                         jit_scalar_t *args, tlab_t *tlab)
{
   uint8_t left[] = { args[1].integer };
   args[6].integer = args[4].integer;
   args[5].integer = args[3].integer;
   args[4].pointer = args[2].pointer;
   args[1].pointer = left;
   args[2].integer = 0;
   args[3].integer = 1;

   synopsys_plus_unsigned(func, anchor, args, tlab);
}

static void synopsys_minus_unsigned(jit_func_t *func, jit_anchor_t *anchor,
                                    jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[3].integer);
   const int rsize = ffi_array_length(args[6].integer);
   const uint8_t *left = args[1].pointer;
   const uint8_t *right = args[4].pointer;

   const int length = MAX(lsize, rsize);
   uint8_t *result = __tlab_alloc(tlab, length, 8);

   if (unlikely(length == 0))
      fail_in_interpreter(func, anchor, args, tlab);

   left = __conv_unsigned(func, anchor, tlab, left, lsize, length);
   right = __conv_unsigned(func, anchor, tlab, right, rsize, length);

   if (unlikely(left[0] == _X || right[0] == _X))
      memset(result, _X, length);
   else {
      __invert_bits(right, length, result);
      (*ieee_packed_add)(left, result, length, 1, result);
   }

   args[0].pointer = result;
   args[1].integer = length - 1;
   args[2].integer = ~length;
}

static void synopsys_mul_unsigned(jit_func_t *func, jit_anchor_t *anchor,
                                  jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[3].integer);
   const int rsize = ffi_array_length(args[6].integer);
   const uint8_t *left = args[1].pointer;
   const uint8_t *right = args[4].pointer;

   if (unlikely(lsize == 0 || rsize == 0))
      fail_in_interpreter(func, anchor, args, tlab);

   left = __conv_unsigned(func, anchor, tlab, left, lsize, lsize);
   right = __conv_unsigned(func, anchor, tlab, right, rsize, rsize);

   const int length = lsize + rsize;
   uint8_t *pa = __tlab_alloc(tlab, length, 8);

   if (unlikely(left[0] == _X || right[0] == _X))
      memset(pa, _X, length);
   else {
      const uint32_t mark = __tlab_mark(tlab);
      __mul_unsigned(left, lsize, right, rsize, tlab, pa);

      __tlab_restore(tlab, mark);
   }

   args[0].pointer = pa;
   args[1].integer = length - 1;
   args[2].integer = ~length;
}

static void synopsys_mul_signed(jit_func_t *func, jit_anchor_t *anchor,
                                jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[3].integer);
   const int rsize = ffi_array_length(args[6].integer);
   const uint8_t *left = args[1].pointer;
   const uint8_t *right = args[4].pointer;

   if (unlikely(lsize == 0 || rsize == 0))
      fail_in_interpreter(func, anchor, args, tlab);

   left = __conv_signed(func, anchor, tlab, left, lsize, lsize);
   right = __conv_signed(func, anchor, tlab, right, rsize, rsize);

   const int length = lsize + rsize;
   uint8_t *pa = __tlab_alloc(tlab, length, 8);

   if (unlikely(left[0] == _X || right[0] == _X))
      memset(pa, _X, length);
   else {
      const uint32_t mark = __tlab_mark(tlab);
      __mul_signed(left, lsize, right, rsize, tlab, pa);

      __tlab_restore(tlab, mark);
   }

   args[0].pointer = pa;
   args[1].integer = length - 1;
   args[2].integer = ~length;
}

static void synopsys_eql_unsigned(jit_func_t *func, jit_anchor_t *anchor,
                                  jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[3].integer);
   const int rsize = ffi_array_length(args[6].integer);
   const uint8_t *left = args[1].pointer;
   const uint8_t *right = args[4].pointer;

   const int length = MAX(lsize, rsize);

   if (unlikely(length == 0))
      fail_in_interpreter(func, anchor, args, tlab);

   const uint32_t mark = __tlab_mark(tlab);

   left = __conv_unsigned(func, anchor, tlab, left, lsize, length);
   right = __conv_unsigned(func, anchor, tlab, right, rsize, length);

   args[0].integer = (memcmp(left, right, length) == 0);

   __tlab_restore(tlab, mark);
}

static void synopsys_eql_signed(jit_func_t *func, jit_anchor_t *anchor,
                                jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[3].integer);
   const int rsize = ffi_array_length(args[6].integer);
   const uint8_t *left = args[1].pointer;
   const uint8_t *right = args[4].pointer;

   const int length = MAX(lsize, rsize);

   if (unlikely(length == 0))
      fail_in_interpreter(func, anchor, args, tlab);

   const uint32_t mark = __tlab_mark(tlab);

   left = __conv_signed(func, anchor, tlab, left, lsize, length);
   right = __conv_signed(func, anchor, tlab, right, rsize, length);

   args[0].integer = (memcmp(left, right, length) == 0);

   __tlab_restore(tlab, mark);
}

#ifdef HAVE_SSE41
__attribute__((target("sse4.1")))
static void byte_vector_equal_sse41(jit_func_t *func, jit_anchor_t *anchor,
                                    jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[2].integer);
   const int rsize = ffi_array_length(args[5].integer);
   uint8_t *left = args[0].pointer;
   uint8_t *right = args[3].pointer;

   args[0].integer = 0;

   if (lsize != rsize)
      return;

   __m128i allmask = _mm_set1_epi8(0xff);

   int pos = 0;
   for (; pos + 15 < lsize; pos += 16) {
      __m128i left1  = _mm_loadu_si128((const __m128i *)(left + pos));
      __m128i right1 = _mm_loadu_si128((const __m128i *)(right + pos));
      __m128i xor    = _mm_xor_si128(left1, right1);
      if (!_mm_test_all_zeros(xor, allmask))
         return;
   }

   if (pos < lsize) {
      __m128i iota   = _mm_load_si128((const __m128i *)lane_iota);
      __m128i mask   = _mm_cmplt_epi8(iota, _mm_set1_epi8(lsize - pos));
      __m128i left1  = _mm_loadu_si128((const __m128i *)(left + pos));
      __m128i right1 = _mm_loadu_si128((const __m128i *)(right + pos));
      __m128i xor    = _mm_xor_si128(left1, right1);
      if (!_mm_test_all_zeros(xor, mask))
         return;
   }

   args[0].integer = 1;
}
#endif

#ifdef HAVE_NEON
static void byte_vector_equal_neon(jit_func_t *func, jit_anchor_t *anchor,
                                   jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[2].integer);
   const int rsize = ffi_array_length(args[5].integer);
   uint8_t *left = args[0].pointer;
   uint8_t *right = args[3].pointer;

   args[0].integer = 0;

   if (lsize != rsize)
      return;

   int pos = 0;
   for (; pos + 15 < lsize; pos += 16) {
      uint8x16_t left1  = vld1q_u8(left + pos);
      uint8x16_t right1 = vld1q_u8(right + pos);
      uint8x16_t xor    = veorq_u8(left1, right1);
      uint64x2_t cast   = vreinterpretq_u64_u8(xor);
      if (vgetq_lane_u64(cast, 0) || vgetq_lane_u64(cast, 1))
         return;
   }

   if (pos < lsize) {
      uint8x16_t iota   = vld1q_u8(lane_iota);
      uint8x16_t mask   = vcltq_u8(iota, vdupq_n_u8(lsize - pos));
      uint8x16_t left1  = vld1q_u8(left + pos);
      uint8x16_t right1 = vld1q_u8(right + pos);
      uint8x16_t xor    = veorq_u8(left1, right1);
      uint8x16_t and    = vandq_u8(xor, mask);
      uint64x2_t cast   = vreinterpretq_u64_u8(and);
      if (vgetq_lane_u64(cast, 0) || vgetq_lane_u64(cast, 1))
         return;
   }

   args[0].integer = 1;
}
#endif

static void byte_vector_equal(jit_func_t *func, jit_anchor_t *anchor,
                              jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[2].integer);
   const int rsize = ffi_array_length(args[5].integer);
   uint8_t *left = args[0].pointer;
   uint8_t *right = args[3].pointer;

   args[0].integer = (lsize == rsize) && (memcmp(left, right, lsize) == 0);
}

static void ieee_math_sin(jit_func_t *func, jit_anchor_t *anchor,
                          jit_scalar_t *args, tlab_t *tlab)
{
   // IEEE Std 1076.2 prescribes exact zero at multiples of pi
   const double x = args[1].real, k = round(x / M_PI);
   args[0].real = (isfinite(x) && k * M_PI == x) ? 0.0 : sin(x);
}

static void ieee_math_cos(jit_func_t *func, jit_anchor_t *anchor,
                          jit_scalar_t *args, tlab_t *tlab)
{
   // Also exact zero at odd multiples of pi / 2
   const double x = args[1].real, k = round(x / M_PI_2);
   args[0].real = (isfinite(x) && fmod(k, 2.0) != 0.0 && k * M_PI_2 == x)
      ? 0.0 : cos(x);
}

static void ieee_math_log(jit_func_t *func, jit_anchor_t *anchor,
                          jit_scalar_t *args, tlab_t *tlab)
{
   args[0].real = log(args[1].real);
}

static void ieee_math_log2(jit_func_t *func, jit_anchor_t *anchor,
                          jit_scalar_t *args, tlab_t *tlab)
{
   args[0].real = log2(args[1].real);
}

static void ieee_math_log10(jit_func_t *func, jit_anchor_t *anchor,
                            jit_scalar_t *args, tlab_t *tlab)
{
   args[0].real = log10(args[1].real);
}

static void ieee_math_round(jit_func_t *func, jit_anchor_t *anchor,
                            jit_scalar_t *args, tlab_t *tlab)
{
   args[0].real = round(args[1].real);
}

static void ieee_math_trunc(jit_func_t *func, jit_anchor_t *anchor,
                            jit_scalar_t *args, tlab_t *tlab)
{
   args[0].real = trunc(args[1].real);
}

static void ieee_math_floor(jit_func_t *func, jit_anchor_t *anchor,
                            jit_scalar_t *args, tlab_t *tlab)
{
   args[0].real = floor(args[1].real);
}

static void ieee_math_pow_real(jit_func_t *func, jit_anchor_t *anchor,
                               jit_scalar_t *args, tlab_t *tlab)
{
   args[0].real = pow(args[1].real, args[2].real);
}

static void ieee_math_pow_integer(jit_func_t *func, jit_anchor_t *anchor,
                                  jit_scalar_t *args, tlab_t *tlab)
{
   args[0].real = pow((double)args[1].integer, args[2].real);
}

static void ieee_math_exp(jit_func_t *func, jit_anchor_t *anchor,
                          jit_scalar_t *args, tlab_t *tlab)
{
   args[0].real = exp(args[1].real);
}

static void std_textio_consume(jit_func_t *func, jit_anchor_t *anchor,
                               jit_scalar_t *args, tlab_t *tlab)
{
   ffi_uarray_t **line = args[2].pointer;
   const int nchars = args[3].integer;

   assert(*line != NULL);

   const int length = ffi_array_length((*line)->dims[0].length);
   assert(nchars <= length);

   (*line)->ptr += nchars;
   (*line)->dims[0].left = 1;
   (*line)->dims[0].length = length - nchars;

   args[0].pointer = NULL;
}

static void std_textio_shrink(jit_func_t *func, jit_anchor_t *anchor,
                              jit_scalar_t *args, tlab_t *tlab)
{
   ffi_uarray_t **line = args[2].pointer;
   const int nchars = args[3].integer;

   assert(*line != NULL);

   (*line)->dims[0].left = 1;
   (*line)->dims[0].length = nchars;

   args[0].pointer = NULL;
}

#define UU "36IEEE.NUMERIC_STD.UNRESOLVED_UNSIGNED"
#define U "25IEEE.NUMERIC_STD.UNSIGNED"
#define US "34IEEE.NUMERIC_STD.UNRESOLVED_SIGNED"
#define S "23IEEE.NUMERIC_STD.SIGNED"
#define NS "IEEE.NUMERIC_STD."
#define SL "IEEE.STD_LOGIC_1164."
#define MR "IEEE.MATH_REAL."
#define ST "STD.STANDARD."
#define TI "STD.TEXTIO."
#define LN "15STD.TEXTIO.LINE"
#define SA "IEEE.STD_LOGIC_ARITH."
#define SU "IEEE.STD_LOGIC_UNSIGNED."
#define SS "IEEE.STD_LOGIC_SIGNED."
#define AU "29IEEE.STD_LOGIC_ARITH.UNSIGNED"
#define AS "27IEEE.STD_LOGIC_ARITH.SIGNED"

static int sv_cmdline_pos = 0;

static void sv_event_wait(jit_func_t *func, jit_anchor_t *caller,
                          jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_event_wait(args[0].pointer);
   args[0].integer = 0;
}

static void sv_queue_wait(jit_func_t *func, jit_anchor_t *caller,
                          jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_queue_wait(args[0].pointer);
   args[0].integer = 0;
}

static void sv_program_begin(jit_func_t *func, jit_anchor_t *caller,
                             jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = x_sv_program_begin();
}

static void sv_sva_yield(jit_func_t *func, jit_anchor_t *caller,
                         jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = x_sv_sva_yield();
}

static void sv_reactive_yield(jit_func_t *func, jit_anchor_t *caller,
                              jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = x_sv_reactive_yield();
}

static void sv_program_end(jit_func_t *func, jit_anchor_t *caller,
                           jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_program_end();
   args[0].integer = 0;
}

static void sv_poll_watch(jit_func_t *func, jit_anchor_t *caller,
                          jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_poll_watch(args[0].pointer, args[1].integer);
   args[0].integer = 0;
}

static void sv_poll_wait(jit_func_t *func, jit_anchor_t *caller,
                         jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_poll_wait();
   args[0].integer = 0;
}

static void sv_poll_fired(jit_func_t *func, jit_anchor_t *caller,
                          jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_poll_fired();
   args[0].integer = 0;
}

static void sv_event_triggered(jit_func_t *func, jit_anchor_t *caller,
                               jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = x_sv_event_triggered(args[0].pointer);
}

static void sv_cmdline_arg(jit_func_t *func, jit_anchor_t *caller,
                           jit_scalar_t *args, tlab_t *tlab)
{
   // Length of the next command line argument after optionally restarting

   if (args[0].integer != 0)
      sv_cmdline_pos = 0;

   const char *arg = vpi_get_plusarg(sv_cmdline_pos);
   args[0].integer = arg != NULL ? strlen(arg) : 0;
}

static const char *sv_plusarg_found = NULL;

static void sv_plusarg_find(jit_func_t *func, jit_anchor_t *caller,
                            jit_scalar_t *args, tlab_t *tlab)
{
   // Find a plusarg starting with the text before the conversion in the format
   // and return one more than the length of the rest of it or zero

   const char *fmt = args[0].pointer;
   const int64_t len = ffi_array_length(args[2].integer);

   int64_t plen = 0;
   for (; plen < len && fmt[plen] != '%'; plen++);

   sv_plusarg_found = NULL;

   for (int i = 0;; i++) {
      const char *arg = vpi_get_plusarg(i);
      if (arg == NULL)
         break;
      else if (arg[0] == '+' && strncmp(arg + 1, fmt, plen) == 0) {
         sv_plusarg_found = arg + 1 + plen;
         break;
      }
   }

   args[0].integer = sv_plusarg_found ? strlen(sv_plusarg_found) + 1 : 0;
}

static void sv_plusarg_copy(jit_func_t *func, jit_anchor_t *caller,
                            jit_scalar_t *args, tlab_t *tlab)
{
   memcpy(args[0].pointer, sv_plusarg_found, strlen(sv_plusarg_found));
   args[0].integer = 0;
}

static void sv_cmdline_copy(jit_func_t *func, jit_anchor_t *caller,
                            jit_scalar_t *args, tlab_t *tlab)
{
   const char *arg = vpi_get_plusarg(sv_cmdline_pos++);
   memcpy(args[0].pointer, arg, strlen(arg));
   args[0].integer = 0;
}

static void sv_regcomp(jit_func_t *func, jit_anchor_t *caller,
                       jit_scalar_t *args, tlab_t *tlab)
{
   // Compile an extended regular expression or return null

#ifndef HAVE_POSIX_REGEX
   args[0].pointer = NULL;
#else
   const int64_t len = ffi_array_length(args[1].integer);
   char *pattern LOCAL = xmalloc(len + 1);
   memcpy(pattern, args[0].pointer, len);
   pattern[len] = '\0';

   regex_t *re = xmalloc(sizeof(regex_t));
   if (regcomp(re, pattern, REG_NOSUB | REG_EXTENDED) != 0) {
      free(re);
      re = NULL;
   }

   args[0].pointer = re;
#endif
}

static void sv_regexec(jit_func_t *func, jit_anchor_t *caller,
                       jit_scalar_t *args, tlab_t *tlab)
{
   // Zero if the string matches

#ifndef HAVE_POSIX_REGEX
   args[0].integer = 1;
#else
   const int64_t len = ffi_array_length(args[2].integer);
   char *str LOCAL = xmalloc(len + 1);
   memcpy(str, args[1].pointer, len);
   str[len] = '\0';

   args[0].integer = regexec(args[0].pointer, str, 0, NULL, 0);
#endif
}

static void sv_regfree(jit_func_t *func, jit_anchor_t *caller,
                       jit_scalar_t *args, tlab_t *tlab)
{
#ifdef HAVE_POSIX_REGEX
   if (args[0].pointer != NULL) {
      regfree(args[0].pointer);
      free(args[0].pointer);
   }
#endif

   args[0].integer = 0;
}

static void sv_nba_store(jit_func_t *func, jit_anchor_t *caller,
                         jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_nba_store(args[0].pointer, args[1].pointer, args[2].integer,
                  args[3].integer, args[4].integer, args[5].integer,
                  args[6].integer);
   args[0].integer = 0;
}

static void sv_event_trigger(jit_func_t *func, jit_anchor_t *caller,
                             jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_event_trigger(args[0].pointer);
   args[0].integer = 0;
}

static void sv_event_trigger_nba(jit_func_t *func, jit_anchor_t *caller,
                                 jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_event_trigger_nba(args[0].pointer, args[1].integer);
   args[0].integer = 0;
}

static void sv_spawn(jit_func_t *func, jit_anchor_t *caller,
                     jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_spawn(args[0].pointer, args[1].pointer, args[2].integer);
   args[0].integer = 0;
}

static void sv_sched_postponed(jit_func_t *func, jit_anchor_t *caller,
                               jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_sched_postponed(args[0].integer != 0);
   args[0].integer = 0;
}

static void sv_fork_begin(jit_func_t *func, jit_anchor_t *caller,
                          jit_scalar_t *args, tlab_t *tlab)
{
   args[0].pointer = x_sv_fork_begin();
}

static void sv_fork_end(jit_func_t *func, jit_anchor_t *caller,
                        jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_fork_end(args[0].pointer);
   args[0].integer = 0;
}

static void sv_fork_wait(jit_func_t *func, jit_anchor_t *caller,
                         jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = x_sv_fork_wait(args[0].pointer, args[1].integer);
}

static void sv_fork_finish(jit_func_t *func, jit_anchor_t *caller,
                           jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_fork_finish();
   args[0].integer = 0;
}

static void sv_fork_disable(jit_func_t *func, jit_anchor_t *caller,
                            jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_fork_disable();
   args[0].integer = 0;
}

static void sv_fork_disable_group(jit_func_t *func, jit_anchor_t *caller,
                                  jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = x_sv_fork_disable_group(args[0].integer, args[1].integer);
}

static void sv_fork_disabled(jit_func_t *func, jit_anchor_t *caller,
                             jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = x_sv_fork_disabled();
}

static void sv_wait_mark(jit_func_t *func, jit_anchor_t *caller,
                         jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_wait_mark(args[0].integer, args[1].integer);
   args[0].integer = 0;
}

static void sv_fork_name(jit_func_t *func, jit_anchor_t *caller,
                         jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_fork_name(args[0].pointer, args[1].integer);
   args[0].integer = 0;
}

static void sv_fork_disable_name(jit_func_t *func, jit_anchor_t *caller,
                                 jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = x_sv_fork_disable_name(args[0].integer);
}

static void sv_defer_disable(jit_func_t *func, jit_anchor_t *caller,
                             jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = x_sv_defer_disable(args[0].integer);
}

static void sv_process_self(jit_func_t *func, jit_anchor_t *caller,
                            jit_scalar_t *args, tlab_t *tlab)
{
   args[0].pointer = x_sv_process_self();
}

static void sv_process_status(jit_func_t *func, jit_anchor_t *caller,
                              jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = x_sv_process_status(args[0].pointer);
}

static void sv_process_kill(jit_func_t *func, jit_anchor_t *caller,
                            jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = x_sv_process_kill(args[0].pointer);
}

static void sv_process_suspend(jit_func_t *func, jit_anchor_t *caller,
                               jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = x_sv_process_suspend(args[0].pointer);
}

static void sv_process_resume(jit_func_t *func, jit_anchor_t *caller,
                              jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = x_sv_process_resume(args[0].pointer);
}

static void sv_process_await(jit_func_t *func, jit_anchor_t *caller,
                             jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = x_sv_process_await(args[0].pointer);
}

static void sv_str_eq(jit_func_t *func, jit_anchor_t *caller,
                      jit_scalar_t *args, tlab_t *tlab)
{
   const int64_t llen = ffi_array_length(args[2].integer);
   const int64_t rlen = ffi_array_length(args[5].integer);

   args[0].integer = llen == rlen
      && (llen == 0 || memcmp(args[0].pointer, args[3].pointer, llen) == 0);
}

static void sv_assoc_badkey(jit_func_t *func, jit_anchor_t *caller,
                            jit_scalar_t *args, tlab_t *tlab)
{
   const int width = args[0].integer;
   const uint64_t abits = args[1].integer, bbits = args[2].integer;

   // See 1800-2023 section 21.2.1.5 for the display of unknown values
   char digits[17];
   int ndigits = 0;
   for (int pos = (width + 3) / 4 - 1; pos >= 0; pos--) {
      const int start = pos * 4, nbits = MIN(4, width - start);
      int nx = 0, nz = 0;
      for (int i = start; i < start + nbits; i++) {
         const bool a = (abits >> i) & 1, b = (bbits >> i) & 1;
         if (b && a)
            nx++;
         else if (b)
            nz++;
      }

      char d;
      if (nx == nbits)
         d = 'x';
      else if (nz == nbits)
         d = 'z';
      else if (nx > 0)
         d = 'X';
      else if (nz > 0)
         d = 'Z';
      else
         d = "0123456789abcdef"[(abits >> start) & 0xf];

      if (ndigits > 0 || d != '0' || pos == 0)
         digits[ndigits++] = d;
   }
   digits[ndigits] = '\0';

   // 7.8.6: an index with x or z bits is invalid and gives a warning
   jit_msg(NULL, DIAG_WARN, "associative array index %d'h%s has x or z "
           "bits", width, digits);
   args[0].integer = 0;
}

static void sv_array_badwrite(jit_func_t *func, jit_anchor_t *caller,
                              jit_scalar_t *args, tlab_t *tlab)
{
   const bool is_queue = args[0].integer, known = args[1].integer;
   const int64_t index = args[2].integer, size = args[3].integer;

   const char *what = is_queue ? "queue" : "dynamic array";
   if (known)
      jit_msg(NULL, DIAG_WARN, "write to index %"PRIi64" of a %s of size "
              "%"PRIi64" is ignored", index, what, size);
   else
      jit_msg(NULL, DIAG_WARN, "write to a %s with an index containing x or "
              "z bits is ignored", what);

   args[0].integer = 0;
}

static void sv_sva_new(jit_func_t *func, jit_anchor_t *caller,
                       jit_scalar_t *args, tlab_t *tlab)
{
   args[0].pointer = sva_new(args[0].integer, args[1].integer);
}

static void sv_sva_label(jit_func_t *func, jit_anchor_t *caller,
                         jit_scalar_t *args, tlab_t *tlab)
{
   const int64_t len = ffi_array_length(args[3].integer);
   char *copy LOCAL = xmalloc(len + 1);
   memcpy(copy, args[1].pointer, len);
   copy[len] = '\0';
   sva_set_label(args[0].pointer, copy);
   args[0].integer = 0;
}

static void sv_sva_control(jit_func_t *func, jit_anchor_t *caller,
                           jit_scalar_t *args, tlab_t *tlab)
{
   const int64_t len = ffi_array_length(args[2].integer);
   char *copy LOCAL = xmalloc(len + 1);
   memcpy(copy, args[0].pointer, len);
   copy[len] = '\0';
   sva_control(args[3].integer, args[4].integer, args[5].integer,
               args[6].integer, copy);
   args[0].integer = 0;
}

static void sv_sva_control_all(jit_func_t *func, jit_anchor_t *caller,
                               jit_scalar_t *args, tlab_t *tlab)
{
   sva_control(args[0].integer, args[1].integer, args[2].integer,
               args[3].integer, NULL);
   args[0].integer = 0;
}

static void sv_sva_node(jit_func_t *func, jit_anchor_t *caller,
                        jit_scalar_t *args, tlab_t *tlab)
{
   sva_t *s = args[0].pointer;
   args[0].integer = sva_node(s, args[1].integer, args[2].integer,
                              args[3].integer, args[4].integer,
                              args[5].integer, args[6].integer);
}

static void sv_sva_config(jit_func_t *func, jit_anchor_t *caller,
                          jit_scalar_t *args, tlab_t *tlab)
{
   sva_config(args[0].pointer, args[1].integer, args[2].integer,
              args[3].integer);
   args[0].integer = 0;
}

static void sv_sva_expr(jit_func_t *func, jit_anchor_t *caller,
                        jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = sva_expr(args[0].pointer, args[1].integer,
                              args[2].integer, args[3].integer,
                              args[4].integer, args[5].integer,
                              args[6].integer, args[7].integer);
}

static void sv_sva_item(jit_func_t *func, jit_anchor_t *caller,
                        jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = sva_item(args[0].pointer, args[1].integer,
                              args[2].integer, args[3].integer,
                              args[4].integer, args[5].integer);
}

static void sv_sva_slot(jit_func_t *func, jit_anchor_t *caller,
                        jit_scalar_t *args, tlab_t *tlab)
{
   sva_slot(args[0].pointer, args[1].integer, args[2].integer,
            args[3].integer);
   args[0].integer = 0;
}

static void sv_sva_slotw(jit_func_t *func, jit_anchor_t *caller,
                         jit_scalar_t *args, tlab_t *tlab)
{
   // The value occupies two slots holding the pointers to the bits
   sva_slotw(args[0].pointer, args[1].integer, args[2].integer,
             args[3].pointer, args[5].integer != 0);
   args[0].integer = 0;
}

static void sv_sva_localw(jit_func_t *func, jit_anchor_t *caller,
                          jit_scalar_t *args, tlab_t *tlab)
{
   sva_localw(args[0].pointer, args[1].integer, args[2].integer);
   args[0].integer = 0;
}

static void sv_sva_root(jit_func_t *func, jit_anchor_t *caller,
                        jit_scalar_t *args, tlab_t *tlab)
{
   sva_set_root(args[0].pointer, args[1].integer);
   args[0].integer = 0;
}

static bool sva_call_thunk(void *jit, int handle, void *ctx,
                           const int64_t *in, int nargs, int64_t *result)
{
   jit_scalar_t argv[SVA_MAX_FARGS + 1], out[1];
   argv[0].pointer = ctx;
   for (int i = 0; i < nargs; i++)
      argv[i + 1].integer = in[i];

   tlab_t tlab = jit_null_tlab(jit);
   if (!jit_vfastcall(jit, handle, argv, nargs + 1, out, 1, &tlab))
      return false;

   *result = out[0].integer;
   return true;
}

static void sv_sva_callfn(jit_func_t *func, jit_anchor_t *caller,
                          jit_scalar_t *args, tlab_t *tlab)
{
   const ffi_closure_t *c = args[1].pointer;
   sva_set_callfn(args[0].pointer, args[2].integer, c->handle,
                  c->args[0].pointer);
   args[0].integer = 0;
}

static void sv_sva_tick(jit_func_t *func, jit_anchor_t *caller,
                        jit_scalar_t *args, tlab_t *tlab)
{
   sva_set_caller(sva_call_thunk, func->jit);
   args[0].integer = sva_tick(args[0].pointer, args[1].integer,
                              args[2].integer);
}

static void sv_sva_wanted(jit_func_t *func, jit_anchor_t *caller,
                          jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = sva_wanted(args[0].pointer, args[1].integer);
}

static void sv_sva_slotwanted(jit_func_t *func, jit_anchor_t *caller,
                              jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = sva_slot_wanted(args[0].pointer, args[1].integer,
                                     args[2].integer);
}

static void sv_sva_finish(jit_func_t *func, jit_anchor_t *caller,
                          jit_scalar_t *args, tlab_t *tlab)
{
   sva_set_caller(sva_call_thunk, func->jit);
   args[0].integer = sva_finish(args[0].pointer);
}

static void sv_sva_flushing(jit_func_t *func, jit_anchor_t *caller,
                            jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = x_sv_sva_flushing();
}

static void sv_sva_result(jit_func_t *func, jit_anchor_t *caller,
                          jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = sva_result(args[0].pointer, args[1].integer);
}

static void sv_sva_nevents(jit_func_t *func, jit_anchor_t *caller,
                           jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = sva_nevents(args[0].pointer);
}

static void sv_sva_event_call(jit_func_t *func, jit_anchor_t *caller,
                              jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = sva_event_call(args[0].pointer, args[1].integer);
}

static void sv_sva_event_local(jit_func_t *func, jit_anchor_t *caller,
                               jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = sva_event_local(args[0].pointer, args[1].integer,
                                     args[2].integer);
}

static void sv_sva_idle(jit_func_t *func, jit_anchor_t *caller,
                        jit_scalar_t *args, tlab_t *tlab)
{
   sva_set_caller(sva_call_thunk, func->jit);
   args[0].integer = sva_idle(args[0].pointer);
}

static void sv_urandom_below(jit_func_t *func, jit_anchor_t *caller,
                             jit_scalar_t *args, tlab_t *tlab)
{
   static __thread int32_t seed;
   INIT_ONCE(seed = opt_verilog_seed());

   const uint64_t range = args[0].integer;
   const uint32_t r = rtl_dist_uniform(&seed, INT32_MIN, INT32_MAX);
   args[0].integer = range == 0 ? 0 : r % range;
}

static void sv_str_cmp(jit_func_t *func, jit_anchor_t *caller,
                       jit_scalar_t *args, tlab_t *tlab)
{
   const uint8_t *left = args[0].pointer, *right = args[3].pointer;
   const int64_t llen = ffi_array_length(args[2].integer);
   const int64_t rlen = ffi_array_length(args[5].integer);
   const bool icase = args[6].integer;

   int64_t result = 0;
   for (int64_t i = 0; i < MIN(llen, rlen) && result == 0; i++) {
      int lc = left[i], rc = right[i];
      if (icase) {
         lc = tolower(lc);
         rc = tolower(rc);
      }
      result = lc - rc;
   }

   args[0].integer = result != 0 ? result : llen - rlen;
}

static void sv_str_ato(jit_func_t *func, jit_anchor_t *caller,
                       jit_scalar_t *args, tlab_t *tlab)
{
   const uint8_t *str = args[0].pointer;
   const int64_t len = ffi_array_length(args[2].integer);
   const int base = args[3].integer;

   int64_t i = 0, result = 0;
   const bool negative = len > 0 && str[0] == '-';
   if (negative)
      i++;

   for (; i < len; i++) {
      int digit;
      if (str[i] == '_')
         continue;
      else if (str[i] >= '0' && str[i] <= '9')
         digit = str[i] - '0';
      else if (str[i] >= 'a' && str[i] <= 'f')
         digit = str[i] - 'a' + 10;
      else if (str[i] >= 'A' && str[i] <= 'F')
         digit = str[i] - 'A' + 10;
      else
         break;

      if (digit >= base)
         break;

      result = result * base + digit;
   }

   args[0].integer = negative ? -result : result;
}

static void sv_str_atoreal(jit_func_t *func, jit_anchor_t *caller,
                           jit_scalar_t *args, tlab_t *tlab)
{
   const uint8_t *str = args[0].pointer;
   const int64_t len = ffi_array_length(args[2].integer);

   char buf[128];
   int n = 0;
   for (int64_t i = 0; i < len && n < sizeof(buf) - 1; i++) {
      if (str[i] != '_')
         buf[n++] = str[i];
   }
   buf[n] = '\0';

   args[0].real = strtod(buf, NULL);
}

static void sv_str_toa(jit_func_t *func, jit_anchor_t *caller,
                       jit_scalar_t *args, tlab_t *tlab)
{
   const int64_t value = args[0].integer;
   const int base = args[1].integer;
   char *dest = args[2].pointer;

   int len;
   switch (base) {
   case 16: len = snprintf(dest, 72, "%"PRIx64, value); break;
   case 8:  len = snprintf(dest, 72, "%"PRIo64, value); break;
   case 2:
      {
         uint64_t bits = value;
         len = 0;
         for (int i = 63; i >= 0; i--) {
            if (len > 0 || ((bits >> i) & 1) || i == 0)
               dest[len++] = '0' + ((bits >> i) & 1);
         }
      }
      break;
   default: len = snprintf(dest, 72, "%"PRIi64, value); break;
   }

   args[0].integer = len;
}

static void sv_str_realtoa(jit_func_t *func, jit_anchor_t *caller,
                           jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = snprintf(args[1].pointer, 72, "%g", args[0].real);
}

static void sv_str_getc(jit_func_t *func, jit_anchor_t *caller,
                        jit_scalar_t *args, tlab_t *tlab)
{
   const uint8_t *str = args[0].pointer;
   const int64_t len = ffi_array_length(args[2].integer);
   const int64_t index = args[3].integer;

   args[0].integer = index >= 0 && index < len ? str[index] : 0;
}

static void sv_str_putc(jit_func_t *func, jit_anchor_t *caller,
                        jit_scalar_t *args, tlab_t *tlab)
{
   uint8_t *str = args[0].pointer;
   const int64_t len = args[1].integer;
   const int64_t index = args[2].integer;
   const uint8_t ch = args[3].integer;

   if (index >= 0 && index < len && ch != 0)
      str[index] = ch;

   args[0].integer = 0;
}

static void sv_str_case(jit_func_t *func, jit_anchor_t *caller,
                        jit_scalar_t *args, tlab_t *tlab)
{
   const uint8_t *src = args[0].pointer;
   const int64_t len = ffi_array_length(args[2].integer);
   uint8_t *dest = args[3].pointer;
   const bool upper = args[4].integer;

   for (int64_t i = 0; i < len; i++)
      dest[i] = upper ? toupper(src[i]) : tolower(src[i]);

   args[0].integer = 0;
}

static int32_t dist_call(int kind, int32_t *seed, int32_t a, int32_t b)
{
   switch (kind) {
   case 1:  return rtl_dist_normal(seed, a, b);
   case 2:  return rtl_dist_exponential(seed, a);
   case 3:  return rtl_dist_poisson(seed, a);
   case 4:  return rtl_dist_chi_square(seed, a);
   case 5:  return rtl_dist_t(seed, a);
   case 6:  return rtl_dist_erlang(seed, a, b);
   default: return rtl_dist_uniform(seed, a, b);
   }
}

static void sv_dist(jit_func_t *func, jit_anchor_t *caller,
                    jit_scalar_t *args, tlab_t *tlab)
{
   int32_t seed = args[1].integer;
   args[0].integer = dist_call(args[0].integer, &seed, args[2].integer,
                               args[3].integer);
}

static void sv_dist_next(jit_func_t *func, jit_anchor_t *caller,
                         jit_scalar_t *args, tlab_t *tlab)
{
   int32_t seed = args[1].integer;
   (void)dist_call(args[0].integer, &seed, args[2].integer, args[3].integer);
   args[0].integer = seed;
}

static void sv_dist_uniform(jit_func_t *func, jit_anchor_t *caller,
                            jit_scalar_t *args, tlab_t *tlab)
{
   int32_t seed = args[0].integer;
   args[0].integer = rtl_dist_uniform(&seed, args[1].integer, args[2].integer);
}

static void sv_dist_seed(jit_func_t *func, jit_anchor_t *caller,
                         jit_scalar_t *args, tlab_t *tlab)
{
   int32_t seed = args[0].integer;
   (void)rtl_dist_uniform(&seed, args[1].integer, args[2].integer);
   args[0].integer = seed;
}

static void sv_crv(jit_func_t *func, jit_anchor_t *caller, jit_scalar_t *args,
                   tlab_t *tlab)
{
   int64_t argv[6];
   for (int i = 0; i < ARRAY_LEN(argv); i++)
      argv[i] = args[i + 1].integer;

   args[0].integer = crv_generated(args[0].integer, argv);
}

static void sv_logic_to_std(jit_func_t *func, jit_anchor_t *caller,
                            jit_scalar_t *args, tlab_t *tlab)
{
   // Strength is held in the upper bits of a Verilog logic value
   static const uint8_t map[4] = { 2 /* 0 */, 3 /* 1 */, 4 /* Z */, 1 /* X */ };

   uint8_t *dst = args[0].pointer;
   const uint8_t *src = args[1].pointer;
   const int64_t count = args[2].integer;

   for (int64_t i = 0; i < count; i++)
      dst[i] = map[src[i] & 3];

   args[0].integer = 0;
}

static void sv_std_to_logic(jit_func_t *func, jit_anchor_t *caller,
                            jit_scalar_t *args, tlab_t *tlab)
{
   // U X 0 1 Z W L H -
   static const uint8_t map[9] = { 3, 3, 0, 1, 2, 3, 0, 1, 3 };

   uint8_t *dst = args[0].pointer;
   const uint8_t *src = args[1].pointer;
   const int64_t count = args[2].integer;

   for (int64_t i = 0; i < count; i++)
      dst[i] = map[src[i]];

   args[0].integer = 0;
}

static void sv_cstr_len(jit_func_t *func, jit_anchor_t *caller,
                        jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = strlen(args[0].pointer);
}

static void sv_fread(jit_func_t *func, jit_anchor_t *caller,
                     jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = x_sv_fread(args[0].integer, args[1].integer);
}

static void sv_fread_count(jit_func_t *func, jit_anchor_t *caller,
                           jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = x_sv_fread_count();
}

static void sv_hier_scope(jit_func_t *func, jit_anchor_t *caller,
                          jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_hier_scope(args[0].pointer);
   args[0].integer = 0;
}

static void sv_vif_child(jit_func_t *func, jit_anchor_t *caller,
                         jit_scalar_t *args, tlab_t *tlab)
{
   args[0].pointer = x_sv_vif_child(args[0].pointer, args[1].integer);
}

static void sv_class_oid(jit_func_t *func, jit_anchor_t *caller,
                         jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = x_sv_class_oid();
}

static void sv_dpi_call(jit_func_t *func, jit_anchor_t *caller,
                        jit_scalar_t *args, tlab_t *tlab)
{
   void *argv[64];
   const int nargs = args[4].integer;
   for (int i = 0; i < nargs; i++)
      argv[i] = args[5 + i].pointer;

   args[0].integer = dpi_call(args[0].pointer, args[1].integer,
                              args[2].pointer, args[3].pointer, argv);
}

static void sv_dpi_resume(jit_func_t *func, jit_anchor_t *caller,
                          jit_scalar_t *args, tlab_t *tlab)
{
   void *argv[64];
   const int nargs = args[1].integer;
   for (int i = 0; i < nargs; i++)
      argv[i] = args[2 + i].pointer;

   args[0].integer = dpi_resume(args[0].integer, argv);
}

static void sv_dpi_export(jit_func_t *func, jit_anchor_t *caller,
                          jit_scalar_t *args, tlab_t *tlab)
{
   dpi_export(func->jit, args[0].pointer, args[1].pointer, args[2].integer,
              args[3].pointer, args[4].integer);
   args[0].integer = 0;
}

static void sv_dpi_oa_make(jit_func_t *func, jit_anchor_t *caller,
                           jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = dpi_oa_make(args);
}

static void sv_dpi_strlen(jit_func_t *func, jit_anchor_t *caller,
                          jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = dpi_string_length(args[0].integer);
}

static void sv_dpi_strcopy(jit_func_t *func, jit_anchor_t *caller,
                           jit_scalar_t *args, tlab_t *tlab)
{
   dpi_string_copy(args[0].integer, args[1].pointer);
   args[0].integer = 0;
}

static void sv_wildcard_eq(jit_func_t *func, jit_anchor_t *caller,
                           jit_scalar_t *args, tlab_t *tlab)
{
   const uint8_t *left = args[0].pointer, *right = args[1].pointer;
   const int64_t count = args[2].integer;

   bool unknown = false;
   for (int64_t i = 0; i < count; i++) {
      if (right[i] >= 2)
         continue;
      else if (left[i] >= 2)
         unknown = true;
      else if (left[i] != right[i]) {
         args[0].integer = 0;
         return;
      }
   }

   args[0].integer = unknown ? 2 : 1;
}

static void sv_real_to_int(jit_func_t *func, jit_anchor_t *caller,
                           jit_scalar_t *args, tlab_t *tlab)
{
   double x = args[0].real;
   if (!isfinite(x)) {
      args[0].integer = 0;
      return;
   }

   x = round(x);

   if (fabs(x) < 9.2e18)
      args[0].integer = (int64_t)x;
   else {
      // The rounded value does not fit: keep its low 64 bits, the two's
      // complement wrap of a conversion to an integral type (6.12.1)
      double m = fmod(x, 18446744073709551616.0);
      if (m < 0.0)
         m += 18446744073709551616.0;
      args[0].integer = (int64_t)(uint64_t)m;
   }
}

static void sv_real_finite(jit_func_t *func, jit_anchor_t *caller,
                           jit_scalar_t *args, tlab_t *tlab)
{
   // A NaN or infinity converted to an integral value has no defined
   // result (6.12.1) so it is reported once for each place it happens

   static uint64_t *seen = NULL;
   static size_t nseen = 0, maxseen = 0;

   const double x = args[0].real;
   const bool four_state = args[1].integer != 0;

   args[0].integer = isfinite(x);
   if (args[0].integer)
      return;

   const uint64_t key = caller == NULL ? 0
      : ((uintptr_t)caller->func << 20) ^ caller->irpos;
   for (size_t i = 0; i < nseen; i++) {
      if (seen[i] == key)
         return;
   }

   if (nseen == maxseen) {
      maxseen = MAX(16, maxseen * 2);
      seen = xrealloc_array(seen, maxseen, sizeof(uint64_t));
   }
   seen[nseen++] = key;

   jit_msg(NULL, DIAG_WARN, "%s converted to an integral value gives %s",
           isnan(x) ? "NaN" : "infinity",
           four_state ? "an unknown value" : "zero");
}

static void sv_real_to_word(jit_func_t *func, jit_anchor_t *caller,
                            jit_scalar_t *args, tlab_t *tlab)
{
   // Word N counting from the least significant of the two's complement
   // representation of the real rounded to an integer

   double x = args[0].real;
   const int64_t n = args[1].integer;

   if (!isfinite(x)) {
      args[0].integer = 0;
      return;
   }

   x = round(x);

   if (fabs(x) < 9.2e18) {
      const int64_t v = (int64_t)x;
      args[0].integer = n == 0 ? v : (v < 0 ? -1 : 0);
      return;
   }

   // The magnitude is mant << shift with a 53-bit mantissa
   int exp;
   const double frac = frexp(fabs(x), &exp);
   const uint64_t mant = (uint64_t)ldexp(frac, 53);
   const int64_t shift = exp - 53;
   const int64_t rel = shift - 64 * n;

   uint64_t word = 0;
   if (rel >= 0 && rel < 64)
      word = mant << rel;
   else if (rel < 0 && rel > -64)
      word = mant >> -rel;

   if (x < 0.0) {
      // Negate: complement and add the carry out of the lower words,
      // which is one only if all of them are zero
      const bool carry = shift + __builtin_ctzll(mant) >= 64 * n;
      word = ~word + (carry ? 1 : 0);
   }

   args[0].integer = word;
}

static void sv_words_to_real(jit_func_t *func, jit_anchor_t *caller,
                             jit_scalar_t *args, tlab_t *tlab)
{
   // A 128-bit two's complement value converted with the host rounding

   const uint64_t hi = args[0].integer, lo = args[1].integer;
   const bool neg = args[2].integer != 0 && (int64_t)hi < 0;

   unsigned __int128 v = ((unsigned __int128)hi << 64) | lo;
   if (neg)
      v = -v;

   const double r = (double)v;
   args[0].real = neg ? -r : r;
}

static void sv_bits_to_real(jit_func_t *func, jit_anchor_t *caller,
                            jit_scalar_t *args, tlab_t *tlab)
{
   uint64_t bits = args[0].integer;
   double real;
   memcpy(&real, &bits, sizeof(real));
   args[0].real = real;
}

static jit_intrinsic_t intrinsic_list[] = {
   { "__sv_bits_to_real", sv_bits_to_real },
   { "__sv_real_to_int", sv_real_to_int },
   { "__sv_real_to_word", sv_real_to_word },
   { "__sv_words_to_real", sv_words_to_real },
   { "__sv_real_finite", sv_real_finite },
   { "__sv_wildcard_eq", sv_wildcard_eq },
   { "__sv_dpi_call", sv_dpi_call },
   { "__sv_dpi_resume", sv_dpi_resume },
   { "__sv_dpi_export", sv_dpi_export },
   { "__sv_dpi_oa_make", sv_dpi_oa_make },
   { "__sv_dpi_strlen", sv_dpi_strlen },
   { "__sv_dpi_strcopy", sv_dpi_strcopy },
   { "__sv_class_oid", sv_class_oid },
   { "__sv_hier_scope", sv_hier_scope },
   { "__sv_vif_child", sv_vif_child },
   { "__sv_event_wait", sv_event_wait },
   { "__sv_queue_wait", sv_queue_wait },
   { "__sv_program_begin", sv_program_begin },
   { "__sv_reactive_yield", sv_reactive_yield },
   { "__sv_sva_yield", sv_sva_yield },
   { "__sv_program_end", sv_program_end },
   { "__sv_poll_watch", sv_poll_watch },
   { "__sv_poll_wait", sv_poll_wait },
   { "__sv_poll_fired", sv_poll_fired },
   { "__sv_nba_store", sv_nba_store },
   { "__sv_cmdline_arg", sv_cmdline_arg },
   { "__sv_plusarg_find", sv_plusarg_find },
   { "__sv_plusarg_copy", sv_plusarg_copy },
   { "__sv_cmdline_copy", sv_cmdline_copy },
   { "__sv_regcomp", sv_regcomp },
   { "__sv_regexec", sv_regexec },
   { "__sv_regfree", sv_regfree },
   { "__sv_event_triggered", sv_event_triggered },
   { "__sv_event_trigger", sv_event_trigger },
   { "__sv_event_trigger_nba", sv_event_trigger_nba },
   { "__sv_spawn", sv_spawn },
   { "__sv_sched_postponed", sv_sched_postponed },
   { "__sv_fork_begin", sv_fork_begin },
   { "__sv_fork_end", sv_fork_end },
   { "__sv_fork_wait", sv_fork_wait },
   { "__sv_fork_disable", sv_fork_disable },
   { "__sv_fork_disable_group", sv_fork_disable_group },
   { "__sv_fork_disabled", sv_fork_disabled },
   { "__sv_wait_mark", sv_wait_mark },
   { "__sv_fork_name", sv_fork_name },
   { "__sv_fork_disable_name", sv_fork_disable_name },
   { "__sv_defer_disable", sv_defer_disable },
   { "__sv_process_self", sv_process_self },
   { "__sv_process_status", sv_process_status },
   { "__sv_process_kill", sv_process_kill },
   { "__sv_process_await", sv_process_await },
   { "__sv_process_suspend", sv_process_suspend },
   { "__sv_process_resume", sv_process_resume },
   { "__sv_fork_finish", sv_fork_finish },
   { "__sv_str_eq", sv_str_eq },
   { "__sv_urandom_below", sv_urandom_below },
   { "__sv_assoc_badkey", sv_assoc_badkey },
   { "__sv_array_badwrite", sv_array_badwrite },
   { "__sv_sva_new", sv_sva_new },
   { "__sv_sva_node", sv_sva_node },
   { "__sv_sva_label", sv_sva_label },
   { "__sv_sva_control", sv_sva_control },
   { "__sv_sva_control_all", sv_sva_control_all },
   { "__sv_sva_config", sv_sva_config },
   { "__sv_sva_expr", sv_sva_expr },
   { "__sv_sva_item", sv_sva_item },
   { "__sv_sva_slot", sv_sva_slot },
   { "__sv_sva_slotw", sv_sva_slotw },
   { "__sv_sva_localw", sv_sva_localw },
   { "__sv_sva_root", sv_sva_root },
   { "__sv_sva_tick", sv_sva_tick },
   { "__sv_sva_callfn", sv_sva_callfn },
   { "__sv_sva_result", sv_sva_result },
   { "__sv_sva_wanted", sv_sva_wanted },
   { "__sv_sva_slotwanted", sv_sva_slotwanted },
   { "__sv_sva_finish", sv_sva_finish },
   { "__sv_sva_flushing", sv_sva_flushing },
   { "__sv_sva_idle", sv_sva_idle },
   { "__sv_sva_nevents", sv_sva_nevents },
   { "__sv_sva_event_call", sv_sva_event_call },
   { "__sv_sva_event_local", sv_sva_event_local },
   { "__sv_str_cmp", sv_str_cmp },
   { "__sv_str_ato", sv_str_ato },
   { "__sv_str_atoreal", sv_str_atoreal },
   { "__sv_str_toa", sv_str_toa },
   { "__sv_str_realtoa", sv_str_realtoa },
   { "__sv_str_case", sv_str_case },
   { "__sv_str_getc", sv_str_getc },
   { "__sv_str_putc", sv_str_putc },
   { "__sv_dist_uniform", sv_dist_uniform },
   { "__sv_dist", sv_dist },
   { "__sv_dist_next", sv_dist_next },
   { "__sv_dist_seed", sv_dist_seed },
   { "__sv_crv", sv_crv },
   { "__sv_logic_to_std", sv_logic_to_std },
   { "__sv_std_to_logic", sv_std_to_logic },
   { "__sv_cstr_len", sv_cstr_len },
   { "__sv_fread", sv_fread },
   { "__sv_fread_count", sv_fread_count },
   { NS "\"+\"(" U U ")" U, ieee_plus_unsigned },
   { NS "\"+\"(" UU UU ")" UU, ieee_plus_unsigned },
   { NS "\"+\"(" U "N)" U, ieee_plus_unsigned_natural },
   { NS "\"+\"(" UU "N)" UU, ieee_plus_unsigned_natural },
   { NS "\"+\"(N" U ")" U, ieee_plus_natural_unsigned },
   { NS "\"+\"(N" UU ")" UU, ieee_plus_natural_unsigned },
   { NS "\"+\"(" S S ")" S, ieee_plus_signed },
   { NS "\"+\"(" US US ")" US, ieee_plus_signed },
   { NS "\"-\"(" U U ")" U, ieee_minus_unsigned },
   { NS "\"-\"(" UU UU ")" UU, ieee_minus_unsigned },
   { NS "\"-\"(" S S ")" S, ieee_minus_signed },
   { NS "\"-\"(" US US ")" US, ieee_minus_signed },
   { NS "\"*\"(" U U ")" U, ieee_mul_unsigned },
   { NS "\"*\"(" UU UU ")" UU, ieee_mul_unsigned },
   { NS "\"*\"(" S S ")" S, ieee_mul_signed },
   { NS "\"*\"(" US US ")" US, ieee_mul_signed },
   { NS "DIVMOD(" UU UU UU UU ")", ieee_divmod },
   { NS "DIVMOD(" U U U U ")", ieee_divmod },
   { NS "\"<\"(" U U ")B", ieee_less_unsigned },
   { NS "\"<\"(" UU UU ")B" , ieee_less_unsigned },
   { NS "\">\"(" U U ")B", ieee_greater_unsigned },
   { NS "\">\"(" UU UU ")B" , ieee_greater_unsigned },
   { NS "\">=\"(" U U ")B", ieee_geq_unsigned },
   { NS "\">=\"(" UU UU ")B" , ieee_geq_unsigned },
   { NS "\"<=\"(" U U ")B", ieee_leq_unsigned },
   { NS "\"<=\"(" UU UU ")B" , ieee_leq_unsigned },
   { NS "TO_INTEGER(" U ")N", ieee_to_integer_unsigned },
   { NS "TO_INTEGER(" UU ")N", ieee_to_integer_unsigned },
   { NS "TO_INTEGER(" S ")I", ieee_to_integer_signed },
   { NS "TO_INTEGER(" US ")I", ieee_to_integer_signed },
#ifdef HAVE_SSE41
   { SL "TO_X01(V)V", std_to_x01_sse41, CPU_SSE41 },
   { SL "TO_X01(Y)Y", std_to_x01_sse41, CPU_SSE41 },
#endif
   { SL "TO_X01(V)V", std_to_x01 },
   { SL "TO_X01(Y)Y", std_to_x01 },
   { NS "TO_01(" U "L)" U, ieee_to_01 },
   { NS "TO_01(" UU "U)" UU, ieee_to_01 },
   { NS "TO_01(" S "L)" U, ieee_to_01 },
   { NS "TO_01(" US "U)" UU, ieee_to_01 },
   { NS "RESIZE(" U "N)" U, ieee_resize_unsigned },
   { NS "RESIZE(" UU "N)" UU, ieee_resize_unsigned },
   { NS "RESIZE(" S "N)" S, ieee_resize_signed },
   { NS "RESIZE(" US "N)" US, ieee_resize_signed },
#ifdef HAVE_SSE41
   { SL "\"and\"(VV)V", ieee_and_vector_sse41, CPU_SSE41 },
   { SL "\"and\"(YY)Y", ieee_and_vector_sse41, CPU_SSE41 },
#endif
#ifdef HAVE_NEON
   { SL "\"and\"(VV)V", ieee_and_vector_neon, CPU_NEON },
   { SL "\"and\"(YY)Y", ieee_and_vector_neon, CPU_NEON },
#endif
   { SL "\"and\"(VV)V", ieee_and_vector },
   { SL "\"and\"(YY)Y", ieee_and_vector },
#ifdef HAVE_SSE41
   { SL "\"or\"(VV)V", ieee_or_vector_sse41, CPU_SSE41 },
   { SL "\"or\"(YY)Y", ieee_or_vector_sse41, CPU_SSE41 },
#endif
#ifdef HAVE_NEON
   { SL "\"or\"(VV)V", ieee_or_vector_neon, CPU_NEON },
   { SL "\"or\"(YY)Y", ieee_or_vector_neon, CPU_NEON },
#endif
   { SL "\"or\"(VV)V", ieee_or_vector },
   { SL "\"or\"(YY)Y", ieee_or_vector },
#ifdef HAVE_SSE41
   { SL "\"xor\"(VV)V", ieee_xor_vector_sse41, CPU_SSE41 },
   { SL "\"xor\"(YY)Y", ieee_xor_vector_sse41, CPU_SSE41 },
#endif
#ifdef HAVE_NEON
   { SL "\"xor\"(VV)V", ieee_xor_vector_neon, CPU_NEON },
   { SL "\"xor\"(YY)Y", ieee_xor_vector_neon, CPU_NEON },
#endif
   { SL "\"xor\"(VV)V", std_xor_vector },
   { SL "\"xor\"(YY)Y", std_xor_vector },
#ifdef HAVE_SSE41
   { SL "\"not\"(V)V", ieee_not_vector_sse41, CPU_SSE41 },
   { SL "\"not\"(Y)Y", ieee_not_vector_sse41, CPU_SSE41 },
#endif
   { NS "TO_UNSIGNED(NN)" U, ieee_to_unsigned },
   { NS "TO_UNSIGNED(NN)" UU, ieee_to_unsigned },
   { NS "TO_SIGNED(IN)" S, ieee_to_signed },
   { NS "TO_SIGNED(IN)" US, ieee_to_signed },
#ifdef HAVE_SSE41
   { SL "\"=\"(VV)B$predef", byte_vector_equal_sse41, CPU_SSE41 },
   { SL "\"=\"(YY)B$predef", byte_vector_equal_sse41, CPU_SSE41 },
   { ST "\"=\"(QQ)B$predef", byte_vector_equal_sse41, CPU_SSE41 },
   { ST "\"=\"(SS)B$predef", byte_vector_equal_sse41, CPU_SSE41 },
#endif
#ifdef HAVE_NEON
   { SL "\"=\"(VV)B$predef", byte_vector_equal_neon, CPU_NEON },
   { SL "\"=\"(YY)B$predef", byte_vector_equal_neon, CPU_NEON },
   { ST "\"=\"(QQ)B$predef", byte_vector_equal_neon, CPU_NEON },
   { ST "\"=\"(SS)B$predef", byte_vector_equal_neon, CPU_NEON },
#endif
   { SL "\"=\"(VV)B$predef", byte_vector_equal },
   { SL "\"=\"(YY)B$predef", byte_vector_equal },
   { ST "\"=\"(QQ)B$predef", byte_vector_equal },
   { ST "\"=\"(SS)B$predef", byte_vector_equal },
   { MR "SIN(R)R", ieee_math_sin },
   { MR "COS(R)R", ieee_math_cos },
   { MR "LOG_LIBM(R)R", ieee_math_log },
   { MR "LOG2_LIBM(R)R", ieee_math_log2 },
   { MR "LOG10_LIBM(R)R", ieee_math_log10 },
   { MR "ROUND(R)R", ieee_math_round },
   { MR "TRUNC(R)R", ieee_math_trunc },
   { MR "FLOOR(R)R", ieee_math_floor },
   { MR "POW_RR_LIBM(RR)R", ieee_math_pow_real },
   { MR "POW_IR_LIBM(IR)R", ieee_math_pow_integer },
   { MR "EXP(R)R", ieee_math_exp },
   { TI "CONSUME(" LN "N)", std_textio_consume },
   { TI "SHRINK(" LN "N)", std_textio_shrink },
   { SA "\"+\"(" AU AU ")" AU, synopsys_plus_unsigned },
   { SA "\"+\"(" AU AU ")V", synopsys_plus_unsigned },
   { SU "\"+\"(VV)V", synopsys_plus_unsigned },
   { SU "\"+\"(YY)Y", synopsys_plus_unsigned },
   { SA "\"+\"(" AS AS ")" AS, synopsys_plus_signed },
   { SA "\"+\"(" AS AS ")V", synopsys_plus_signed },
   { SS "\"+\"(VV)V", synopsys_plus_signed },
   { SS "\"+\"(YY)Y", synopsys_plus_signed },
   { SA "\"+\"(" AU "U)" AU, synopsys_plus_unsigned_logic },
   { SA "\"+\"(" AU "U)V", synopsys_plus_unsigned_logic },
   { SU "\"+\"(VL)V", synopsys_plus_unsigned_logic },
   { SU "\"+\"(YL)Y", synopsys_plus_unsigned_logic },
   { SA "\"+\"(U" AU ")" AU, synopsys_plus_logic_unsigned },
   { SA "\"+\"(U" AU ")V", synopsys_plus_logic_unsigned },
   { SU "\"+\"(LV)V", synopsys_plus_logic_unsigned },
   { SU "\"+\"(LY)Y", synopsys_plus_logic_unsigned },
   { SA "\"-\"(" AU AU ")" AU, synopsys_minus_unsigned },
   { SA "\"-\"(" AU AU ")V", synopsys_minus_unsigned },
   { SU "\"-\"(VV)V", synopsys_minus_unsigned },
   { SU "\"-\"(YY)Y", synopsys_minus_unsigned },
   { SA "\"*\"(" AU AU ")" AU, synopsys_mul_unsigned },
   { SA "\"*\"(" AU AU ")V", synopsys_mul_unsigned },
   { SU "\"*\"(VV)V", synopsys_mul_unsigned },
   { SU "\"*\"(YY)Y", synopsys_mul_unsigned },
   { SA "\"*\"(" AS AS ")" AS, synopsys_mul_signed },
   { SA "\"*\"(" AS AS ")V", synopsys_mul_signed },
   { SS "\"*\"(VV)V", synopsys_mul_signed },
   { SS "\"*\"(YY)Y", synopsys_mul_signed },
   { SA "\"=\"(" AU AU ")B", synopsys_eql_unsigned },
   { SA "\"=\"(" AU AU ")B", synopsys_eql_unsigned },
   { SU "\"=\"(VV)B", synopsys_eql_unsigned },
   { SU "\"=\"(YY)B", synopsys_eql_unsigned },
   { SA "\"=\"(" AS AS ")B", synopsys_eql_signed },
   { SA "\"=\"(" AS AS ")B", synopsys_eql_signed },
   { SS "\"=\"(VV)B", synopsys_eql_signed },
   { SS "\"=\"(YY)B", synopsys_eql_signed },
   { NULL, NULL }
};

static void protected_dispatch_call(jit_func_t *func, jit_anchor_t *anchor,
                                    jit_scalar_t *args, tlab_t *tlab,
                                    int first)
{
   // Call a method through an access to an uninstantiated protected type
   // The arguments are the name of the protected type instance and the
   // suffix of the method name as unconstrained strings followed by the
   // arguments of the method itself.  FIRST is the first slot after the
   // procedure state which is not used for functions.

   size_t len[2];
   const char *str[2];
   for (int i = 0; i < 2; i++) {
      const int64_t n = args[first + i * 3 + 2].integer;
      len[i] = n < 0 ? ~n : n;
      str[i] = args[first + i * 3].pointer;
   }

   char *buf LOCAL = xmalloc(len[0] + len[1] + 1);
   memcpy(buf, str[0], len[0]);
   memcpy(buf + len[0], str[1], len[1]);
   buf[len[0] + len[1]] = '\0';

   jit_handle_t handle = jit_lazy_compile(func->jit, ident_new(buf));
   if (handle == JIT_HANDLE_INVALID)
      fatal_trace("missing definition for %s", buf);

   const int nprefix = 6;
   memmove(args + first, args + first + nprefix,
           (JIT_MAX_ARGS - first - nprefix) * sizeof(*args));

   jit_func_t *f = jit_get_func(func->jit, handle);
   jit_entry_fn_t entry = load_acquire(&f->entry);
   (*entry)(f, anchor, args, tlab);
}

static void protected_dispatch_func(jit_func_t *func, jit_anchor_t *anchor,
                                    jit_scalar_t *args, tlab_t *tlab)
{
   protected_dispatch_call(func, anchor, args, tlab, 0);
}

static void protected_dispatch_proc(jit_func_t *func, jit_anchor_t *anchor,
                                    jit_scalar_t *args, tlab_t *tlab)
{
   protected_dispatch_call(func, anchor, args, tlab, 1);
}

jit_entry_fn_t jit_bind_intrinsic(ident_t name)
{
   static ident_t dispatch_func_i = NULL, dispatch_proc_i = NULL;

   INIT_ONCE({
         dispatch_func_i = ident_new("__nvc_protected_dispatch_func");
         dispatch_proc_i = ident_new("__nvc_protected_dispatch_proc");

         const bool want_intrinsics = !!opt_get_int(OPT_JIT_INTRINSICS);

#if ASAN_ENABLED
         const bool want_vector = false;   // Reads past end of input (benign)
#else
         const bool want_vector = !!opt_get_int(OPT_VECTOR_INTRINSICS);
#endif

         ieee_packed_add = __ieee_packed_add_scalar;

         cpu_feature_t mask = 0;
#ifdef HAVE_SSE41
         if (want_vector && __builtin_cpu_supports("sse4.1")) {
            mask |= CPU_SSE41;
            ieee_packed_add = __ieee_packed_add_sse41;
         }
#endif
#if HAVE_AVX2
         if (want_vector && __builtin_cpu_supports("avx2"))
            mask |= CPU_AVX2;
#endif
#ifdef HAVE_NEON
         if (want_vector)
            mask |= CPU_NEON;
#endif

         for (jit_intrinsic_t *it = intrinsic_list; it->name; it++) {
            if (it->feature && !(it->feature & mask))
               continue;
            else if (want_intrinsics)
               it->ident = ident_new(it->name);
         }
      });

   if (name == dispatch_func_i)
      return protected_dispatch_func;
   else if (name == dispatch_proc_i)
      return protected_dispatch_proc;

   for (const jit_intrinsic_t *it = intrinsic_list; it->name; it++) {
      if (it->ident == name)
         return it->entry;
   }

   return NULL;
}
