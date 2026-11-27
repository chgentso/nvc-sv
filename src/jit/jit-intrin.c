//
//  Copyright (C) 2023-2026  Nick Gasson
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
#include "rt/defs.h"
#include "rt/model.h"
#include "rt/structs.h"
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
            uint64_t: (((x) & UINT64_C(0xfefefefefefefefe))             \
                       == UINT64_C(0x0202020202020202)),                \
            uint8_t: ((x) & 0xfe) == 0x02)

#define IS_01LH(x) \
   _Generic((x),                                                        \
            uint64_t: (((x) & UINT64_C(0xfafafafafafafafa))             \
                       == UINT64_C(0x0202020202020202)),                \
            uint8_t: ((x) & 0xfa) == 0x02)

__attribute__((aligned(16)))
static const uint8_t cvt_to_x01[16] = {
   _X, _X, _0, _1, _X, _X, _0, _1, _X, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

__attribute__((aligned(16)))
static const uint8_t cvt_to_x01z[16] = {
   _X, _X, _0, _1, _Z, _X, _0, _1, _X, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

__attribute__((aligned(16)))
static const uint8_t cvt_to_ux01[16] = {
   _U, _X, _0, _1, _X, _X, _0, _1, _X, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

__attribute__((aligned(16)))
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

__attribute__((aligned(16)))
static const uint8_t xnor_table[16][16] = {
   {   _U, _U, _U, _U, _U, _U, _U, _U, _U   },  // | U |
   {   _U, _X, _X, _X, _X, _X, _X, _X, _X   },  // | X |
   {   _U, _X, _1, _0, _X, _X, _1, _0, _X   },  // | 0 |
   {   _U, _X, _0, _1, _X, _X, _0, _1, _X   },  // | 1 |
   {   _U, _X, _X, _X, _X, _X, _X, _X, _X   },  // | Z |
   {   _U, _X, _X, _X, _X, _X, _X, _X, _X   },  // | W |
   {   _U, _X, _1, _0, _X, _X, _1, _0, _X   },  // | L |
   {   _U, _X, _0, _1, _X, _X, _0, _1, _X   },  // | H |
   {   _U, _X, _X, _X, _X, _X, _X, _X, _X   },  // | - |
};

__attribute__((aligned(16)))
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

__attribute__((aligned(16)))
static const uint8_t nand_table[16][16] = {
   {    _U, _U, _1, _U, _U, _U, _1, _U, _U   },  // | U |
   {    _U, _X, _1, _X, _X, _X, _1, _X, _X   },  // | X |
   {    _1, _1, _1, _1, _1, _1, _1, _1, _1   },  // | 0 |
   {    _U, _X, _1, _0, _X, _X, _1, _0, _X   },  // | 1 |
   {    _U, _X, _1, _X, _X, _X, _1, _X, _X   },  // | Z |
   {    _U, _X, _1, _X, _X, _X, _1, _X, _X   },  // | W |
   {    _1, _1, _1, _1, _1, _1, _1, _1, _1   },  // | L |
   {    _U, _X, _1, _0, _X, _X, _1, _0, _X   },  // | H |
   {    _U, _X, _1, _X, _X, _X, _1, _X, _X   },  // | - |
};

__attribute__((aligned(16)))
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

__attribute__((aligned(16)))
static const uint8_t nor_table[16][16] = {
   {    _U, _U, _U, _0, _U, _U, _U, _0, _U   },  // | U |
   {    _U, _X, _X, _0, _X, _X, _X, _0, _X   },  // | X |
   {    _U, _X, _1, _0, _X, _X, _1, _0, _X   },  // | 0 |
   {    _0, _0, _0, _0, _0, _0, _0, _0, _0   },  // | 1 |
   {    _U, _X, _X, _0, _X, _X, _X, _0, _X   },  // | Z |
   {    _U, _X, _X, _0, _X, _X, _X, _0, _X   },  // | W |
   {    _U, _X, _1, _0, _X, _X, _1, _0, _X   },  // | L |
   {    _0, _0, _0, _0, _0, _0, _0, _0, _0   },  // | H |
   {    _U, _X, _X, _0, _X, _X, _X, _0, _X   },  // | - |
};

__attribute__((aligned(16)))
static const uint8_t not_table[16] = {
   // ---------------------------------------------------
   // |  U   X   0   1   Z   W   L   H   -          |   |
   _U, _X, _1, _0, _X, _X, _1, _0, _X, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

static const uint8_t match_eq_table[9][9] = {
   { _U, _U, _U, _U, _U, _U, _U, _U, _1 },
   { _U, _X, _X, _X, _X, _X, _X, _X, _1 },
   { _U, _X, _1, _0, _X, _X, _1, _0, _1 },
   { _U, _X, _0, _1, _X, _X, _0, _1, _1 },
   { _U, _X, _X, _X, _X, _X, _X, _X, _1 },
   { _U, _X, _X, _X, _X, _X, _X, _X, _1 },
   { _U, _X, _1, _0, _X, _X, _1, _0, _1 },
   { _U, _X, _0, _1, _X, _X, _0, _1, _1 },
   { _1, _1, _1, _1, _1, _1, _1, _1, _1 }
};

__attribute__((aligned(16)))
static const uint8_t or_reduce_prio[16] = {
   2, 1, 0, 3, 1, 1, 0, 3, 1, 0, 0, 0, 0, 0, 0, 0
};

__attribute__((aligned(16)))
static const uint8_t and_reduce_prio[16] = {
   2, 1, 3, 0, 1, 1, 3, 0, 1, 0, 0, 0, 0, 0, 0, 0
};

static const uint8_t or_reduce_unmap[4]   = { _0, _X, _U, _1 };
static const uint8_t nor_reduce_unmap[4]  = { _1, _X, _U, _0 };
static const uint8_t and_reduce_unmap[4]  = { _1, _X, _U, _0 };
static const uint8_t nand_reduce_unmap[4] = { _0, _X, _U, _1 };

#if defined HAVE_AVX2 || defined HAVE_SSE41 || defined HAVE_NEON

// Compressed lookup tables for vectorised intrinsics.  Note the
// vectorised intrinsics all rely on being able to read up to
// OVERRUN_MARGIN (defined in src/rt/mspace.h) bytes beyond the end of
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
   { _U, _U, _U, _1 },
   { _U, _X, _X, _1 },
   { _U, _X, _0, _1 },
   { _1, _1, _1, _1 },
};

__attribute__((aligned(16)))
static const uint8_t small_nor_table[4][4] = {
   { _U, _U, _U, _0 },
   { _U, _X, _X, _0 },
   { _U, _X, _1, _0 },
   { _0, _0, _0, _0 },
};

__attribute__((aligned(16)))
static const uint8_t small_and_table[4][4] = {
   { _U, _U, _0, _U },
   { _U, _X, _0, _X },
   { _0, _0, _0, _0 },
   { _U, _X, _0, _1 },
};

__attribute__((aligned(16)))
static const uint8_t small_nand_table[4][4] = {
   { _U, _U, _1, _U },
   { _U, _X, _1, _X },
   { _1, _1, _1, _1 },
   { _U, _X, _1, _0 },
};

__attribute__((aligned(16)))
static const uint8_t small_xor_table[4][4] = {
   { _U, _U, _U, _U },
   { _U, _X, _X, _X },
   { _U, _X, _0, _1 },
   { _U, _X, _1, _0 },
};

__attribute__((aligned(16)))
static const uint8_t small_xnor_table[4][4] = {
   { _U, _U, _U, _U },
   { _U, _X, _X, _X },
   { _U, _X, _1, _0 },
   { _U, _X, _0, _1 },
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

#ifdef HAVE_AVX2
__attribute__((aligned(32)))
static const uint8_t lane_iota32[32] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
   16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
};

__attribute__((aligned(32)))
static const uint8_t reverse_lane32[32] = {
   15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
   15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0
};

__attribute__((aligned(32)))
static const uint8_t spread_shuffle32[32] = {
   3, 3, 3, 3, 3, 3, 3, 3,  2, 2, 2, 2, 2, 2, 2, 2,
   1, 1, 1, 1, 1, 1, 1, 1,  0, 0, 0, 0, 0, 0, 0, 0
};

__attribute__((aligned(32)))
static const uint8_t spread_mask32[32] = {
   0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01,
   0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01,
   0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01,
   0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01,
};
#endif

static void (*ieee_packed_add)(const uint8_t *, const uint8_t *,
                               int, int, uint8_t *);
static uint8_t *(*ieee_to_01_slow_fn)(tlab_t *, const uint8_t *, int, uint8_t);

__attribute__((always_inline))
static inline void *__tlab_alloc(tlab_t *t, size_t size, size_t align)
{
   assert(t->alloc <= t->limit);
   assert((t->alloc & (sizeof(double) - 1)) == 0);
   assert(align % sizeof(double) == 0);

   // Always allocate a multiple of 32 bytes as the AVX2/SSE4.1 routines
   // may write a full 256-bit vector
   const size_t alignup = ALIGN_UP(size, 32);
   const size_t base = ALIGN_UP(t->alloc, align);
   if (likely(base + alignup <= t->limit)) {
      t->alloc = base + alignup;
      return t->data + base;
   }
   else
      return mspace_alloc(t->mspace, alignup);
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
   int pos = 0;
   for (; pos + 8 <= size; pos += 8) {
      uint64_t w = unaligned_load(input + pos, uint64_t);
      w ^= UINT64_C(0x0101010101010101);
      unaligned_store(result + pos, w, uint64_t);
   }
   for (; pos < size; pos++) {
      assert(input[pos] == _0 || input[pos] == _1);
      result[pos] = input[pos] ^ 1;
   }
}

__attribute__((always_inline))
static inline uint8_t __pack_low_bits(const void *vec)
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

__attribute__((always_inline))
static inline bool __all_01(const void *vec, int size)
{
   int pos = 0;
   for (; pos + 7 < size; pos += 8) {
      const uint64_t u64 = unaligned_load(vec + pos, uint64_t);
      if (unlikely(!IS_01(u64)))
         return false;
   }

   for (; pos < size; pos++) {
      const uint8_t u8 = *(const uint8_t *)(vec + pos);
      if (unlikely(!IS_01(u8)))
         return false;
   }

   return true;
}

__attribute__((always_inline))
static inline bool __is_x_swar(const uint8_t *vec, int size)
{
   int pos = 0;
   for (; pos + 8 <= size; pos += 8) {
      const uint64_t w = unaligned_load(vec + pos, uint64_t);
      if (unlikely(!IS_01LH(w)))
         return true;
   }

   for (; pos < size; pos++) {
      if (unlikely(!IS_01LH(vec[pos])))
         return true;
   }

   return false;
}

__attribute__((always_inline))
static inline bool __try_pack_u64_swar(const uint8_t *arr, int size,
                                       uint64_t *out)
{
   assert(size >= 1 && size <= 64);

   uint64_t val = 0;
   int i = 0;
   for (; i + 8 <= size; i += 8) {
      const uint64_t w = unaligned_load(arr + i, uint64_t);
      if (unlikely(!IS_01(w)))
         return false;
      const uint8_t byte = ((w & UINT64_C(0x0101010101010101))
                            * UINT64_C(0x8040201008040201)) >> 56;
      val = (val << 8) | byte;
   }
   for (; i < size; i++) {
      const uint8_t b = arr[i];
      if (unlikely(!IS_01(b)))
         return false;
      val = (val << 1) | (b & 1);
   }

   *out = val;
   return true;
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
static inline void __unpack_from_u64(uint64_t val, uint8_t *result, int size)
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
static inline int64_t __sign_extend_u64(uint64_t val, int size)
{
   assert(size >= 1 && size <= 64);
   const int sh = 64 - size;
   return (int64_t)(val << sh) >> sh;
}

__attribute__((cold, noinline))
static uint8_t *__ieee_to_01_slow_swar(tlab_t *tlab, const uint8_t *input,
                                       int size, uint8_t xmap)
{
   uint8_t *result = __tlab_alloc(tlab, size, 8);

   int pos = 0;
   for (; pos + 8 <= size; pos += 8) {
      const uint64_t w = unaligned_load(input + pos, uint64_t);
      if (unlikely(!IS_01LH(w))) {
         memset(result, xmap, size);
         return result;
      }
      unaligned_store(result + pos, w & UINT64_C(0x0303030303030303), uint64_t);
   }
   for (; pos < size; pos++) {
      const uint8_t elt = input[pos];
      if (unlikely(!IS_01LH(elt))) {
         memset(result, xmap, size);
         return result;
      }
      result[pos] = elt & 0x03;
   }

   return result;
}

#ifdef HAVE_SSE41
__attribute__((target("sse4.1"), always_inline))
static inline uint16_t __pack16_sse41(__m128i v)
{
   const __m128i reverse = _mm_load_si128((const __m128i *)reverse_lane);
   v = _mm_shuffle_epi8(v, reverse);
   v = _mm_slli_epi16(v, 7);
   return (uint16_t)_mm_movemask_epi8(v);
}

__attribute__((target("sse4.1"), always_inline))
static inline __m128i __spread_bits_16_sse41_vec(uint16_t packed)
{
   const __m128i shuffle = _mm_load_si128((const __m128i *)spread_shuffle);
   const __m128i mask = _mm_load_si128((const __m128i *)spread_mask);
   const __m128i twos = _mm_set1_epi8(2);

   __m128i bits = _mm_set1_epi16((short)packed);
   bits = _mm_shuffle_epi8(bits, shuffle);
   bits = _mm_and_si128(bits, mask);
   bits = _mm_cmpeq_epi8(bits, mask);
   return _mm_sub_epi8(twos, bits);
}

__attribute__((target("sse4.1"), always_inline))
static inline bool __all_01_sse41(const uint8_t *vec, int size)
{
   const __m128i m_fe = _mm_set1_epi8((char)0xfe);
   const __m128i m_02 = _mm_set1_epi8(0x02);

   int pos = 0;
   for (; pos + 16 <= size; pos += 16) {
      __m128i v = _mm_loadu_si128((const __m128i *)(vec + pos));
      __m128i bad = _mm_xor_si128(_mm_and_si128(v, m_fe), m_02);
      if (unlikely(!_mm_testz_si128(bad, bad)))
         return false;
   }
   if (pos < size) {
      const int rem = size - pos;
      __m128i v = _mm_loadu_si128((const __m128i *)(vec + pos));
      __m128i eq = _mm_cmpeq_epi8(_mm_and_si128(v, m_fe), m_02);
      const uint32_t ok = (uint32_t)_mm_movemask_epi8(eq);
      const uint32_t mask = (1U << rem) - 1;
      if (unlikely((~ok & mask) != 0))
         return false;
   }
   return true;
}

__attribute__((target("sse4.1"), always_inline))
static inline bool __is_x_sse41(const uint8_t *vec, int size)
{
   const __m128i m_fa = _mm_set1_epi8((char)0xfa);
   const __m128i m_02 = _mm_set1_epi8(0x02);

   int pos = 0;
   for (; pos + 16 <= size; pos += 16) {
      __m128i v = _mm_loadu_si128((const __m128i *)(vec + pos));
      __m128i bad = _mm_xor_si128(_mm_and_si128(v, m_fa), m_02);
      if (unlikely(!_mm_testz_si128(bad, bad)))
         return true;
   }
   if (pos < size) {
      const int rem = size - pos;
      __m128i v = _mm_loadu_si128((const __m128i *)(vec + pos));
      __m128i eq = _mm_cmpeq_epi8(_mm_and_si128(v, m_fa), m_02);
      const uint32_t ok = (uint32_t)_mm_movemask_epi8(eq);
      const uint32_t mask = (1U << rem) - 1;
      if (unlikely((~ok & mask) != 0))
         return true;
   }
   return false;
}

__attribute__((target("sse4.1"), always_inline))
static inline bool __try_pack_u64_sse41(const uint8_t *arr, int size,
                                        uint64_t *out)
{
   assert(size >= 1 && size <= 64);

   const __m128i m_fe = _mm_set1_epi8((char)0xfe);
   const __m128i m_02 = _mm_set1_epi8(0x02);

   uint64_t val = 0;
   int pos = 0;
   for (; pos + 16 <= size; pos += 16) {
      __m128i v = _mm_loadu_si128((const __m128i *)(arr + pos));
      __m128i bad = _mm_xor_si128(_mm_and_si128(v, m_fe), m_02);
      if (unlikely(!_mm_testz_si128(bad, bad)))
         return false;
      val = (val << 16) | __pack16_sse41(v);
   }
   if (pos < size) {
      const int rem = size - pos;
      __m128i v = _mm_loadu_si128((const __m128i *)(arr + pos));
      __m128i eq = _mm_cmpeq_epi8(_mm_and_si128(v, m_fe), m_02);
      const uint32_t ok = (uint32_t)_mm_movemask_epi8(eq);
      const uint32_t mask = (1U << rem) - 1;
      if (unlikely((~ok & mask) != 0))
         return false;
      const uint32_t lo = __pack16_sse41(v) >> (16 - rem);
      val = (val << rem) | lo;
   }

   *out = val;
   return true;
}

__attribute__((target("sse4.1"), always_inline))
static inline void __unpack_from_u64_sse41(uint64_t val, uint8_t *result,
                                           int size)
{
   assert(size >= 1 && size <= 64);

   int pos = 0;
   int rem = size;
   while (rem > 16) {
      rem -= 16;
      const uint16_t hi = (uint16_t)(val >> rem);
      _mm_storeu_si128((__m128i *)(result + pos),
                       __spread_bits_16_sse41_vec(hi));
      pos += 16;
   }
   const uint16_t lo = (uint16_t)(val << (16 - rem));
   _mm_storeu_si128((__m128i *)(result + pos), __spread_bits_16_sse41_vec(lo));
}

__attribute__((target("sse4.1"), cold, noinline))
static uint8_t *__ieee_to_01_slow_sse41(tlab_t *tlab, const uint8_t *input,
                                        int size, uint8_t xmap)
{
   uint8_t *result = __tlab_alloc(tlab, ALIGN_UP(size, 16), 16);
   const __m128i m_fa = _mm_set1_epi8((char)0xfa);
   const __m128i m_02 = _mm_set1_epi8(0x02);
   const __m128i m_03 = _mm_set1_epi8(0x03);

   int pos = 0;
   for (; pos + 16 <= size; pos += 16) {
      __m128i v = _mm_loadu_si128((const __m128i *)(input + pos));
      __m128i bad = _mm_xor_si128(_mm_and_si128(v, m_fa), m_02);
      if (unlikely(!_mm_testz_si128(bad, bad))) {
         memset(result, xmap, size);
         return result;
      }
      _mm_storeu_si128((__m128i *)(result + pos), _mm_and_si128(v, m_03));
   }
   if (pos < size) {
      const int rem = size - pos;
      __m128i v = _mm_loadu_si128((const __m128i *)(input + pos));
      __m128i eq = _mm_cmpeq_epi8(_mm_and_si128(v, m_fa), m_02);
      const uint32_t ok = (uint32_t)_mm_movemask_epi8(eq);
      const uint32_t mask = (1U << rem) - 1;
      if (unlikely((~ok & mask) != 0)) {
         memset(result, xmap, size);
         return result;
      }
      _mm_storeu_si128((__m128i *)(result + pos), _mm_and_si128(v, m_03));
   }

   return result;
}

__attribute__((target("sse4.1")))
static void __ieee_packed_add_sse41(const uint8_t *left,
                                    const uint8_t *right,
                                    int size, int carry,
                                    uint8_t *result)
{
   const int prefix = size & 15;
   for (int pos = size - 16; pos >= prefix; pos -= 16) {
      __m128i lvec = _mm_loadu_si128((const __m128i *)(left + pos));
      __m128i rvec = _mm_loadu_si128((const __m128i *)(right + pos));

      const unsigned lbits = __pack16_sse41(lvec);
      const unsigned rbits = __pack16_sse41(rvec);
      const unsigned sum = lbits + rbits + carry;

      __m128i spread = __spread_bits_16_sse41_vec((uint16_t)sum);
      _mm_storeu_si128((__m128i *)(result + pos), spread);
      carry = !!(sum & 0x10000);
   }

   if (prefix > 0) {
      __m128i iota = _mm_load_si128((const __m128i *)lane_iota);
      __m128i mask = _mm_cmplt_epi8(iota, _mm_set1_epi8((char)prefix));
      __m128i lvec = _mm_loadu_si128((const __m128i *)left);
      __m128i rvec = _mm_loadu_si128((const __m128i *)right);

      const unsigned lbits = __pack16_sse41(lvec) >> (16 - prefix);
      const unsigned rbits = __pack16_sse41(rvec) >> (16 - prefix);
      const unsigned sum = lbits + rbits + carry;

      __m128i tail = __spread_bits_16_sse41_vec((uint16_t)(sum << (16 - prefix)));
      __m128i prev = _mm_loadu_si128((const __m128i *)result);
      __m128i out = _mm_blendv_epi8(prev, tail, mask);

      _mm_storeu_si128((__m128i *)result, out);
   }
}
#endif

#ifdef HAVE_AVX2
__attribute__((target("avx2"), always_inline))
static inline uint32_t __pack32_avx2(__m256i v)
{
   const __m256i rev = _mm256_load_si256((const __m256i *)reverse_lane32);
   v = _mm256_shuffle_epi8(v, rev);
   v = _mm256_slli_epi16(v, 7);
   const uint32_t m = (uint32_t)_mm256_movemask_epi8(v);
   return (m << 16) | (m >> 16);
}

__attribute__((target("avx2"), always_inline))
static inline __m256i __spread_bits_32_avx2_vec(uint32_t packed)
{
   const __m256i shuffle = _mm256_load_si256((const __m256i *)spread_shuffle32);
   const __m256i mask = _mm256_load_si256((const __m256i *)spread_mask32);
   const __m256i twos = _mm256_set1_epi8(2);

   __m256i bits = _mm256_set1_epi32((int)packed);
   bits = _mm256_shuffle_epi8(bits, shuffle);
   bits = _mm256_and_si256(bits, mask);
   bits = _mm256_cmpeq_epi8(bits, mask);
   return _mm256_sub_epi8(twos, bits);
}

__attribute__((target("avx2"), always_inline))
static inline bool __all_01_avx2(const uint8_t *vec, int size)
{
   const __m256i m_fe = _mm256_set1_epi8((char)0xfe);
   const __m256i m_02 = _mm256_set1_epi8(0x02);

   int pos = 0;
   for (; pos + 32 <= size; pos += 32) {
      __m256i v = _mm256_loadu_si256((const __m256i *)(vec + pos));
      __m256i bad = _mm256_xor_si256(_mm256_and_si256(v, m_fe), m_02);
      if (unlikely(!_mm256_testz_si256(bad, bad)))
         return false;
   }
   if (pos < size) {
      const int rem = size - pos;
      __m256i v = _mm256_loadu_si256((const __m256i *)(vec + pos));
      __m256i eq = _mm256_cmpeq_epi8(_mm256_and_si256(v, m_fe), m_02);
      const uint32_t ok = (uint32_t)_mm256_movemask_epi8(eq);
      const uint32_t mask = ~0U >> (32 - rem);
      if (unlikely((~ok & mask) != 0))
         return false;
   }
   return true;
}

__attribute__((target("avx2"), always_inline))
static inline bool __is_x_avx2(const uint8_t *vec, int size)
{
   const __m256i m_fa = _mm256_set1_epi8((char)0xfa);
   const __m256i m_02 = _mm256_set1_epi8(0x02);

   int pos = 0;
   for (; pos + 32 <= size; pos += 32) {
      __m256i v = _mm256_loadu_si256((const __m256i *)(vec + pos));
      __m256i bad = _mm256_xor_si256(_mm256_and_si256(v, m_fa), m_02);
      if (unlikely(!_mm256_testz_si256(bad, bad)))
         return true;
   }
   if (pos < size) {
      const int rem = size - pos;
      __m256i v = _mm256_loadu_si256((const __m256i *)(vec + pos));
      __m256i eq = _mm256_cmpeq_epi8(_mm256_and_si256(v, m_fa), m_02);
      const uint32_t ok = (uint32_t)_mm256_movemask_epi8(eq);
      const uint32_t mask = ~0U >> (32 - rem);
      if (unlikely((~ok & mask) != 0))
         return true;
   }
   return false;
}

__attribute__((target("avx2"), always_inline))
static inline bool __try_pack_u64_avx2(const uint8_t *arr, int size,
                                       uint64_t *out)
{
   assert(size >= 1 && size <= 64);

   const __m256i m_fe = _mm256_set1_epi8((char)0xfe);
   const __m256i m_02 = _mm256_set1_epi8(0x02);

   if (size <= 32) {
      __m256i v = _mm256_loadu_si256((const __m256i *)arr);
      __m256i eq = _mm256_cmpeq_epi8(_mm256_and_si256(v, m_fe), m_02);
      const uint32_t ok = (uint32_t)_mm256_movemask_epi8(eq);
      const uint32_t mask = ~0U >> (32 - size);
      if (unlikely((~ok & mask) != 0))
         return false;
      *out = (uint64_t)(__pack32_avx2(v) >> (32 - size));
      return true;
   }
   else {
      __m256i v0 = _mm256_loadu_si256((const __m256i *)arr);
      __m256i bad0 = _mm256_xor_si256(_mm256_and_si256(v0, m_fe), m_02);
      if (unlikely(!_mm256_testz_si256(bad0, bad0)))
         return false;

      const int rem = size - 32;
      __m256i v1 = _mm256_loadu_si256((const __m256i *)(arr + 32));
      __m256i eq1 = _mm256_cmpeq_epi8(_mm256_and_si256(v1, m_fe), m_02);
      const uint32_t ok1 = (uint32_t)_mm256_movemask_epi8(eq1);
      const uint32_t mask1 = ~0U >> (32 - rem);
      if (unlikely((~ok1 & mask1) != 0))
         return false;

      const uint32_t hi = __pack32_avx2(v0);
      const uint32_t lo = __pack32_avx2(v1) >> (32 - rem);
      *out = ((uint64_t)hi << rem) | lo;
      return true;
   }
}

__attribute__((target("avx2"), always_inline))
static inline void __unpack_from_u64_avx2(uint64_t val, uint8_t *result,
                                          int size)
{
   assert(size >= 1 && size <= 64);

   if (size <= 32) {
      const uint32_t w = (uint32_t)(val << (32 - size));
      _mm256_storeu_si256((__m256i *)result, __spread_bits_32_avx2_vec(w));
   }
   else {
      const int rem = size - 32;
      const uint32_t hi = (uint32_t)(val >> rem);
      const uint32_t lo = (uint32_t)(val << (32 - rem));
      _mm256_storeu_si256((__m256i *)(result + 0),
                          __spread_bits_32_avx2_vec(hi));
      _mm256_storeu_si256((__m256i *)(result + 32),
                          __spread_bits_32_avx2_vec(lo));
   }
}

__attribute__((target("avx2"), cold, noinline))
static uint8_t *__ieee_to_01_slow_avx2(tlab_t *tlab, const uint8_t *input,
                                       int size, uint8_t xmap)
{
   uint8_t *result = __tlab_alloc(tlab, ALIGN_UP(size, 32), 16);
   const __m256i m_fa = _mm256_set1_epi8((char)0xfa);
   const __m256i m_02 = _mm256_set1_epi8(0x02);
   const __m256i m_03 = _mm256_set1_epi8(0x03);

   int pos = 0;
   for (; pos + 32 <= size; pos += 32) {
      __m256i v = _mm256_loadu_si256((const __m256i *)(input + pos));
      __m256i bad = _mm256_xor_si256(_mm256_and_si256(v, m_fa), m_02);
      if (unlikely(!_mm256_testz_si256(bad, bad))) {
         memset(result, xmap, size);
         return result;
      }
      _mm256_storeu_si256((__m256i *)(result + pos), _mm256_and_si256(v, m_03));
   }
   if (pos < size) {
      const int rem = size - pos;
      __m256i v = _mm256_loadu_si256((const __m256i *)(input + pos));
      __m256i eq = _mm256_cmpeq_epi8(_mm256_and_si256(v, m_fa), m_02);
      const uint32_t ok = (uint32_t)_mm256_movemask_epi8(eq);
      const uint32_t mask = ~0U >> (32 - rem);
      if (unlikely((~ok & mask) != 0)) {
         memset(result, xmap, size);
         return result;
      }
      _mm256_storeu_si256((__m256i *)(result + pos), _mm256_and_si256(v, m_03));
   }

   return result;
}

__attribute__((target("avx2")))
static void __ieee_packed_add_avx2(const uint8_t *left,
                                   const uint8_t *right,
                                   int size, int carry,
                                   uint8_t *result)
{
   const int prefix = size & 31;
   for (int pos = size - 32; pos >= prefix; pos -= 32) {
      __m256i lvec = _mm256_loadu_si256((const __m256i *)(left + pos));
      __m256i rvec = _mm256_loadu_si256((const __m256i *)(right + pos));

      const uint64_t lbits = __pack32_avx2(lvec);
      const uint64_t rbits = __pack32_avx2(rvec);
      const uint64_t sum = lbits + rbits + (uint64_t)carry;

      _mm256_storeu_si256((__m256i *)(result + pos),
                          __spread_bits_32_avx2_vec((uint32_t)sum));
      carry = (int)(sum >> 32);
   }

   if (prefix > 0) {
      __m256i iota = _mm256_load_si256((const __m256i *)lane_iota32);
      __m256i mask = _mm256_cmpgt_epi8(_mm256_set1_epi8((char)prefix), iota);
      __m256i lvec = _mm256_loadu_si256((const __m256i *)left);
      __m256i rvec = _mm256_loadu_si256((const __m256i *)right);

      const uint64_t lbits = __pack32_avx2(lvec) >> (32 - prefix);
      const uint64_t rbits = __pack32_avx2(rvec) >> (32 - prefix);
      const uint64_t sum = lbits + rbits + (uint64_t)carry;

      __m256i tail = __spread_bits_32_avx2_vec((uint32_t)(sum << (32 - prefix)));
      __m256i prev = _mm256_loadu_si256((const __m256i *)result);
      __m256i out = _mm256_blendv_epi8(prev, tail, mask);

      _mm256_storeu_si256((__m256i *)result, out);
   }
}
#endif

__attribute__((always_inline))
static inline uint8_t *__to_01(tlab_t *tlab, const uint8_t *input,
                               int size, uint8_t xmap)
{
   assert(size > 0);

   if (likely(__all_01(input, size)))
      return (uint8_t *)input;
   else
      return (*ieee_to_01_slow_fn)(tlab, input, size, xmap);
}

#define DEFINE_STD_CVT_FAMILY(name, tbl)                                \
   static void name(jit_func_t *func, jit_anchor_t *anchor,             \
                    jit_scalar_t *args, tlab_t *tlab)                   \
   {                                                                    \
      const int size = ffi_array_length(args[3].integer);               \
      const uint8_t *input = args[1].pointer;                           \
      uint8_t *result = __tlab_alloc(tlab, size, 8);                    \
      for (int i = 0; i < size; i++)                                    \
         result[i] = tbl[input[i]];                                     \
      args[0].pointer = result;                                         \
      args[1].integer = 1;                                              \
      args[2].integer = size;                                           \
   }

DEFINE_STD_CVT_FAMILY(std_to_x01, cvt_to_x01)
DEFINE_STD_CVT_FAMILY(std_to_x01z, cvt_to_x01z)
DEFINE_STD_CVT_FAMILY(std_to_ux01, cvt_to_ux01)

#ifdef HAVE_SSE41
#define DEFINE_STD_CVT_SSE41(name, tbl)                                 \
   __attribute__((target("sse4.1")))                                    \
   static void name##_sse41(jit_func_t *func, jit_anchor_t *anchor,     \
                            jit_scalar_t *args, tlab_t *tlab)           \
   {                                                                    \
      const int size = ffi_array_length(args[3].integer);               \
      const uint8_t *input = args[1].pointer;                           \
      uint8_t *result = __tlab_alloc(tlab, ALIGN_UP(size, 16), 16);     \
      const __m128i lookup = _mm_load_si128((const __m128i *)tbl);      \
      for (int pos = 0; pos < size; pos += 16) {                        \
         __m128i in = _mm_loadu_si128((const __m128i *)(input + pos));  \
         _mm_storeu_si128((__m128i *)(result + pos),                    \
                          _mm_shuffle_epi8(lookup, in));                \
      }                                                                 \
      args[0].pointer = result;                                         \
      args[1].integer = 1;                                              \
      args[2].integer = size;                                           \
   }

DEFINE_STD_CVT_SSE41(std_to_x01, cvt_to_x01)
DEFINE_STD_CVT_SSE41(std_to_x01z, cvt_to_x01z)
DEFINE_STD_CVT_SSE41(std_to_ux01, cvt_to_ux01)
#endif

#ifdef HAVE_AVX2
#define DEFINE_STD_CVT_AVX2(name, tbl)                                  \
   __attribute__((target("avx2")))                                      \
   static void name##_avx2(jit_func_t *func, jit_anchor_t *anchor,      \
                           jit_scalar_t *args, tlab_t *tlab)            \
   {                                                                    \
      const int size = ffi_array_length(args[3].integer);               \
      const uint8_t *input = args[1].pointer;                           \
      uint8_t *result = __tlab_alloc(tlab, ALIGN_UP(size, 32), 16);     \
      const __m256i lookup =                                            \
         _mm256_broadcastsi128_si256(_mm_load_si128((const __m128i *)tbl)); \
      for (int pos = 0; pos < size; pos += 32) {                        \
         __m256i in = _mm256_loadu_si256((const __m256i *)(input + pos)); \
         _mm256_storeu_si256((__m256i *)(result + pos),                 \
                             _mm256_shuffle_epi8(lookup, in));          \
      }                                                                 \
      args[0].pointer = result;                                         \
      args[1].integer = 1;                                              \
      args[2].integer = size;                                           \
   }

DEFINE_STD_CVT_AVX2(std_to_x01, cvt_to_x01)
DEFINE_STD_CVT_AVX2(std_to_x01z, cvt_to_x01z)
DEFINE_STD_CVT_AVX2(std_to_ux01, cvt_to_ux01)
#endif

static void std_to_01(jit_func_t *func, jit_anchor_t *anchor,
                      jit_scalar_t *args, tlab_t *tlab)
{
   const int size = ffi_array_length(args[3].integer);
   const uint8_t *input = args[1].pointer;
   const uint8_t xmap = args[4].integer;

   if (unlikely(size == 0)) {
      args[0].pointer = NULL;
      args[1].integer = -1;
      args[2].integer = -1;
   }
   else {
      args[0].pointer = __to_01(tlab, input, size, xmap);
      args[1].integer = size - 1;
      args[2].integer = ~size;
   }
}

#ifdef HAVE_AVX2
__attribute__((target("avx2")))
static void std_to_01_avx2(jit_func_t *func, jit_anchor_t *anchor,
                           jit_scalar_t *args, tlab_t *tlab)
{
   const int size = ffi_array_length(args[3].integer);
   const uint8_t *input = args[1].pointer;
   const uint8_t xmap = args[4].integer;

   if (unlikely(size == 0)) {
      args[0].pointer = NULL;
      args[1].integer = -1;
      args[2].integer = -1;
   }
   else {
      args[0].pointer = likely(__all_01_avx2(input, size))
         ? (uint8_t *)input : __ieee_to_01_slow_avx2(tlab, input, size, xmap);
      args[1].integer = size - 1;
      args[2].integer = ~size;
   }
}
#endif

static void ieee_to_01_unsigned(jit_func_t *func, jit_anchor_t *anchor,
                                jit_scalar_t *args, tlab_t *tlab)
{
   const int size = ffi_array_length(args[3].integer);
   const uint8_t *input = args[1].pointer;
   const uint8_t xmap = args[4].integer;

   if (unlikely(size == 0)) {
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

static void ieee_to_01_signed(jit_func_t *func, jit_anchor_t *anchor,
                              jit_scalar_t *args, tlab_t *tlab)
{
   const int size = ffi_array_length(args[3].integer);
   const uint8_t *input = args[1].pointer;
   const uint8_t xmap = args[4].integer;

   if (unlikely(size == 0)) {
      ieee_warn(func, anchor,
                "NUMERIC_STD.TO_01: null detected, returning NAS");
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

static void std_is_x_scalar(jit_func_t *func, jit_anchor_t *anchor,
                            jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = !IS_01LH((uint8_t)args[1].integer);
}

static void std_is_x_vec(jit_func_t *func, jit_anchor_t *anchor,
                         jit_scalar_t *args, tlab_t *tlab)
{
   const int size = ffi_array_length(args[3].integer);
   const uint8_t *input = args[1].pointer;
   args[0].integer = __is_x_swar(input, size);
}

#ifdef HAVE_SSE41
__attribute__((target("sse4.1")))
static void std_is_x_vec_sse41(jit_func_t *func, jit_anchor_t *anchor,
                               jit_scalar_t *args, tlab_t *tlab)
{
   const int size = ffi_array_length(args[3].integer);
   const uint8_t *input = args[1].pointer;
   args[0].integer = __is_x_sse41(input, size);
}
#endif

#ifdef HAVE_AVX2
__attribute__((target("avx2")))
static void std_is_x_vec_avx2(jit_func_t *func, jit_anchor_t *anchor,
                              jit_scalar_t *args, tlab_t *tlab)
{
   const int size = ffi_array_length(args[3].integer);
   const uint8_t *input = args[1].pointer;
   args[0].integer = __is_x_avx2(input, size);
}
#endif

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
      memset(result, *(const uint8_t *)input, newsize - bound);
      memcpy(result + newsize - bound, (const uint8_t *)input + size - bound,
             bound);

      return result;
   }
}

static void ieee_resize_unsigned(jit_func_t *func, jit_anchor_t *anchor,
                                 jit_scalar_t *args, tlab_t *tlab)
{
   const int size = ffi_array_length(args[3].integer);
   const int newsize = args[4].integer;
   const uint8_t *input = args[1].pointer;

   if (newsize < 1) {
      args[0].pointer = NULL;
      args[1].integer = 0;
      args[2].integer = -1;
      return;
   }

   args[0].pointer = __resize_unsigned(tlab, input, size, newsize);
   args[1].integer = newsize - 1;
   args[2].integer = ~newsize;
}

static void ieee_resize_unsigned_res(jit_func_t *func, jit_anchor_t *anchor,
                                     jit_scalar_t *args, tlab_t *tlab)
{
   const int size = ffi_array_length(args[3].integer);
   const int newsize = ffi_array_length(args[6].integer);
   const uint8_t *input = args[1].pointer;

   if (newsize < 1) {
      args[0].pointer = NULL;
      args[1].integer = 0;
      args[2].integer = -1;
      return;
   }

   args[0].pointer = __resize_unsigned(tlab, input, size, newsize);
   args[1].integer = newsize - 1;
   args[2].integer = ~newsize;
}

static void ieee_resize_signed(jit_func_t *func, jit_anchor_t *anchor,
                               jit_scalar_t *args, tlab_t *tlab)
{
   const int size = ffi_array_length(args[3].integer);
   const int newsize = args[4].integer;
   const uint8_t *input = args[1].pointer;

   if (newsize < 1) {
      args[0].pointer = NULL;
      args[1].integer = 0;
      args[2].integer = -1;
      return;
   }

   args[0].pointer = __resize_signed(tlab, input, size, newsize);
   args[1].integer = newsize - 1;
   args[2].integer = ~newsize;
}

static void ieee_resize_signed_res(jit_func_t *func, jit_anchor_t *anchor,
                                   jit_scalar_t *args, tlab_t *tlab)
{
   const int size = ffi_array_length(args[3].integer);
   const int newsize = ffi_array_length(args[6].integer);
   const uint8_t *input = args[1].pointer;

   if (newsize < 1) {
      args[0].pointer = NULL;
      args[1].integer = 0;
      args[2].integer = -1;
      return;
   }

   args[0].pointer = __resize_signed(tlab, input, size, newsize);
   args[1].integer = newsize - 1;
   args[2].integer = ~newsize;
}

static void __ieee_packed_add_scalar(const uint8_t *left,
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

__attribute__((always_inline))
static inline void __check_to_unsigned_trunc(jit_func_t *func,
                                             jit_anchor_t *anchor,
                                             uint64_t arg, int size)
{
   if (unlikely(size < 64 && (arg >> size) != 0))
      ieee_warn(func, anchor, "NUMERIC_STD.TO_UNSIGNED: vector truncated");
}

__attribute__((always_inline))
static inline void __check_to_signed_trunc(jit_func_t *func,
                                           jit_anchor_t *anchor,
                                           int64_t arg, int size)
{
   if (unlikely(size < 64)) {
      const int64_t hi = arg >> (size - 1);
      if (unlikely(hi != 0 && hi != -1))
         ieee_warn(func, anchor, "NUMERIC_STD.TO_SIGNED: vector truncated");
   }
}

__attribute__((always_inline))
static inline uint8_t *__to_unsigned(jit_func_t *func, jit_anchor_t *anchor,
                                     tlab_t *tlab, uint64_t arg, int size)
{
   __check_to_unsigned_trunc(func, anchor, arg, size);
   uint8_t *result = __tlab_alloc(tlab, size, 8);
   if (size <= 64)
      __unpack_from_u64(arg, result, size);
   else {
      memset(result, _0, size - 64);
      __unpack_from_u64(arg, result + (size - 64), 64);
   }
   return result;
}

__attribute__((always_inline))
static inline uint8_t *__to_signed(jit_func_t *func, jit_anchor_t *anchor,
                                   tlab_t *tlab, int64_t arg, int size)
{
   __check_to_signed_trunc(func, anchor, arg, size);
   uint8_t *result = __tlab_alloc(tlab, size, 8);
   if (size <= 64)
      __unpack_from_u64((uint64_t)arg, result, size);
   else {
      memset(result, arg < 0 ? _1 : _0, size - 64);
      __unpack_from_u64((uint64_t)arg, result + (size - 64), 64);
   }
   return result;
}

// Shifts and rotates (exact 9-state via memcpy/memset)
__attribute__((always_inline))
static inline void __do_shift_left(const uint8_t *arg, int size, int64_t count,
                                   bool is_downto, jit_scalar_t *args,
                                   tlab_t *tlab)
{
   if (unlikely(size == 0)) {
      args[0].pointer = NULL;
      args[1].integer = is_downto ? 0 : 1;
      args[2].integer = is_downto ? -1 : 0;
      return;
   }
   if (count == 0) {
      args[0].pointer = (uint8_t *)arg;
   }
   else {
      uint8_t *result = __tlab_alloc(tlab, size, 8);
      if (count >= size)
         memset(result, _0, size);
      else {
         const int active = size - (int)count;
         memcpy(result, arg + count, active);
         memset(result + active, _0, (size_t)count);
      }
      args[0].pointer = result;
   }
   args[1].integer = is_downto ? size - 1 : 1;
   args[2].integer = is_downto ? ~size : size;
}

__attribute__((always_inline))
static inline void __do_shift_right(const uint8_t *arg, int size, int64_t count,
                                    bool arith, bool is_downto,
                                    jit_scalar_t *args, tlab_t *tlab)
{
   if (unlikely(size == 0)) {
      args[0].pointer = NULL;
      args[1].integer = is_downto ? 0 : 1;
      args[2].integer = is_downto ? -1 : 0;
      return;
   }
   if (count == 0) {
      args[0].pointer = (uint8_t *)arg;
   }
   else {
      uint8_t *result = __tlab_alloc(tlab, size, 8);
      const uint8_t fill = arith ? arg[0] : _0;
      if (count >= size)
         memset(result, fill, size);
      else {
         const int active = size - (int)count;
         memset(result, fill, (size_t)count);
         memcpy(result + count, arg, active);
      }
      args[0].pointer = result;
   }
   args[1].integer = is_downto ? size - 1 : 1;
   args[2].integer = is_downto ? ~size : size;
}

__attribute__((always_inline))
static inline void __do_rotate(const uint8_t *arg, int size, uint64_t count,
                               bool right, bool is_downto,
                               jit_scalar_t *args, tlab_t *tlab)
{
   if (unlikely(size == 0)) {
      args[0].pointer = NULL;
      args[1].integer = is_downto ? 0 : 1;
      args[2].integer = is_downto ? -1 : 0;
      return;
   }
   const int c = (int)(count % (uint64_t)size);
   if (c == 0) {
      args[0].pointer = (uint8_t *)arg;
   }
   else {
      uint8_t *result = __tlab_alloc(tlab, size, 8);
      if (right) {
         memcpy(result, arg + size - c, c);
         memcpy(result + c, arg, size - c);
      }
      else {
         memcpy(result, arg + c, size - c);
         memcpy(result + size - c, arg, c);
      }
      args[0].pointer = result;
   }
   args[1].integer = is_downto ? size - 1 : 1;
   args[2].integer = is_downto ? ~size : size;
}

static void ieee_shift_left(jit_func_t *func, jit_anchor_t *anchor,
                            jit_scalar_t *args, tlab_t *tlab)
{
   __do_shift_left(args[1].pointer, ffi_array_length(args[3].integer),
                   args[4].integer, true, args, tlab);
}

static void ieee_shift_right_unsigned(jit_func_t *func, jit_anchor_t *anchor,
                                      jit_scalar_t *args, tlab_t *tlab)
{
   __do_shift_right(args[1].pointer, ffi_array_length(args[3].integer),
                    args[4].integer, false, true, args, tlab);
}

static void ieee_shift_right_signed(jit_func_t *func, jit_anchor_t *anchor,
                                    jit_scalar_t *args, tlab_t *tlab)
{
   __do_shift_right(args[1].pointer, ffi_array_length(args[3].integer),
                    args[4].integer, true, true, args, tlab);
}

static void ieee_rotate_left(jit_func_t *func, jit_anchor_t *anchor,
                             jit_scalar_t *args, tlab_t *tlab)
{
   __do_rotate(args[1].pointer, ffi_array_length(args[3].integer),
               (uint64_t)args[4].integer, false, true, args, tlab);
}

static void ieee_rotate_right(jit_func_t *func, jit_anchor_t *anchor,
                              jit_scalar_t *args, tlab_t *tlab)
{
   __do_rotate(args[1].pointer, ffi_array_length(args[3].integer),
               (uint64_t)args[4].integer, true, true, args, tlab);
}

static void ieee_sll(jit_func_t *func, jit_anchor_t *anchor,
                     jit_scalar_t *args, tlab_t *tlab)
{
   const int size = ffi_array_length(args[3].integer);
   const int64_t count = args[4].integer;
   if (count >= 0)
      __do_shift_left(args[1].pointer, size, count, true, args, tlab);
   else
      __do_shift_right(args[1].pointer, size, -(count + 1) + 1, false, true,
                       args, tlab);
}

static void ieee_srl(jit_func_t *func, jit_anchor_t *anchor,
                     jit_scalar_t *args, tlab_t *tlab)
{
   const int size = ffi_array_length(args[3].integer);
   const int64_t count = args[4].integer;
   if (count >= 0)
      __do_shift_right(args[1].pointer, size, count, false, true, args, tlab);
   else
      __do_shift_left(args[1].pointer, size, -(count + 1) + 1, true, args, tlab);
}

static void ieee_sla_signed(jit_func_t *func, jit_anchor_t *anchor,
                            jit_scalar_t *args, tlab_t *tlab)
{
   const int size = ffi_array_length(args[3].integer);
   const int64_t count = args[4].integer;
   if (count >= 0)
      __do_shift_left(args[1].pointer, size, count, true, args, tlab);
   else
      __do_shift_right(args[1].pointer, size, -(count + 1) + 1, true, true,
                       args, tlab);
}

static void ieee_sra_signed(jit_func_t *func, jit_anchor_t *anchor,
                            jit_scalar_t *args, tlab_t *tlab)
{
   const int size = ffi_array_length(args[3].integer);
   const int64_t count = args[4].integer;
   if (count >= 0)
      __do_shift_right(args[1].pointer, size, count, true, true, args, tlab);
   else
      __do_shift_left(args[1].pointer, size, -(count + 1) + 1, true, args, tlab);
}

static void ieee_rol(jit_func_t *func, jit_anchor_t *anchor,
                     jit_scalar_t *args, tlab_t *tlab)
{
   const int size = ffi_array_length(args[3].integer);
   const int64_t count = args[4].integer;
   if (count >= 0)
      __do_rotate(args[1].pointer, size, (uint64_t)count, false, true,
                  args, tlab);
   else
      __do_rotate(args[1].pointer, size, (uint64_t)(-(count + 1)) + 1, true,
                  true, args, tlab);
}

static void ieee_ror(jit_func_t *func, jit_anchor_t *anchor,
                     jit_scalar_t *args, tlab_t *tlab)
{
   const int size = ffi_array_length(args[3].integer);
   const int64_t count = args[4].integer;
   if (count >= 0)
      __do_rotate(args[1].pointer, size, (uint64_t)count, true, true,
                  args, tlab);
   else
      __do_rotate(args[1].pointer, size, (uint64_t)(-(count + 1)) + 1, false,
                  true, args, tlab);
}

static void std_sll(jit_func_t *func, jit_anchor_t *anchor,
                    jit_scalar_t *args, tlab_t *tlab)
{
   const int size = ffi_array_length(args[3].integer);
   const int64_t count = args[4].integer;
   if (count >= 0)
      __do_shift_left(args[1].pointer, size, count, false, args, tlab);
   else
      __do_shift_right(args[1].pointer, size, -(count + 1) + 1, false, false,
                       args, tlab);
}

static void std_srl(jit_func_t *func, jit_anchor_t *anchor,
                    jit_scalar_t *args, tlab_t *tlab)
{
   const int size = ffi_array_length(args[3].integer);
   const int64_t count = args[4].integer;
   if (count >= 0)
      __do_shift_right(args[1].pointer, size, count, false, false, args, tlab);
   else
      __do_shift_left(args[1].pointer, size, -(count + 1) + 1, false,
                      args, tlab);
}

static void std_rol(jit_func_t *func, jit_anchor_t *anchor,
                    jit_scalar_t *args, tlab_t *tlab)
{
   const int size = ffi_array_length(args[3].integer);
   if (unlikely(size == 0))
      fail_in_interpreter(func, anchor, args, tlab);
   const int64_t count = args[4].integer;
   if (count >= 0)
      __do_rotate(args[1].pointer, size, (uint64_t)count, false, false,
                  args, tlab);
   else
      __do_rotate(args[1].pointer, size, (uint64_t)(-(count + 1)) + 1, true,
                  false, args, tlab);
}

static void std_ror(jit_func_t *func, jit_anchor_t *anchor,
                    jit_scalar_t *args, tlab_t *tlab)
{
   const int size = ffi_array_length(args[3].integer);
   if (unlikely(size == 0))
      fail_in_interpreter(func, anchor, args, tlab);
   const int64_t count = args[4].integer;
   if (count >= 0)
      __do_rotate(args[1].pointer, size, (uint64_t)count, true, false,
                  args, tlab);
   else
      __do_rotate(args[1].pointer, size, (uint64_t)(-(count + 1)) + 1, false,
                  false, args, tlab);
}

// TO_HSTRING / TO_OSTRING fast paths with exact 9-state fallback
static const char hex_digits[16] = "0123456789ABCDEF";

__attribute__((cold, noinline))
static void __to_hstring_slow(const uint8_t *input, int size, bool is_signed,
                              uint8_t *result, int res_len)
{
   const int pad = res_len * 4 - size;
   const uint8_t pad_val = is_signed ? input[0]
      : (input[0] == _Z ? _Z : _0);

   for (int g = 0; g < res_len; g++) {
      int nbits = 0, nz = 0;
      bool bad = false;
      for (int k = 0; k < 4; k++) {
         const int idx = g * 4 + k - pad;
         const uint8_t raw = idx < 0 ? pad_val : input[idx];
         const uint8_t v = cvt_to_x01z[raw];
         if (v == _0 || v == _1)
            nbits = (nbits << 1) | (v & 1);
         else if (v == _Z)
            nz++;
         else
            bad = true;
      }
      if (!bad && nz == 0)
         result[g] = (uint8_t)hex_digits[nbits];
      else if (!bad && nz == 4)
         result[g] = 'Z';
      else
         result[g] = 'X';
   }
}

__attribute__((cold, noinline))
static void __to_ostring_slow(const uint8_t *input, int size, bool is_signed,
                              uint8_t *result, int res_len)
{
   const int pad = res_len * 3 - size;
   const uint8_t pad_val = is_signed ? input[0]
      : (input[0] == _Z ? _Z : _0);

   for (int g = 0; g < res_len; g++) {
      int nbits = 0, nz = 0;
      bool bad = false;
      for (int k = 0; k < 3; k++) {
         const int idx = g * 3 + k - pad;
         const uint8_t raw = idx < 0 ? pad_val : input[idx];
         const uint8_t v = cvt_to_x01z[raw];
         if (v == _0 || v == _1)
            nbits = (nbits << 1) | (v & 1);
         else if (v == _Z)
            nz++;
         else
            bad = true;
      }
      if (!bad && nz == 0)
         result[g] = (uint8_t)('0' + nbits);
      else if (!bad && nz == 3)
         result[g] = 'Z';
      else
         result[g] = 'X';
   }
}

static void __do_to_hstring_swar(jit_func_t *func, jit_anchor_t *anchor,
                                 jit_scalar_t *args, tlab_t *tlab,
                                 bool is_signed)
{
   const int size = ffi_array_length(args[3].integer);
   const uint8_t *input = args[1].pointer;
   if (unlikely(size == 0))
      fail_in_interpreter(func, anchor, args, tlab);

   const int res_len = (size + 3) / 4;
   uint8_t *result = __tlab_alloc(tlab, res_len, 8);

   if (likely((size & 3) == 0 && __all_01(input, size))) {
      int pos = 0, out = 0;
      for (; pos + 8 <= size; pos += 8) {
         const uint8_t b = __pack_low_bits(input + pos);
         result[out++] = (uint8_t)hex_digits[b >> 4];
         result[out++] = (uint8_t)hex_digits[b & 0xf];
      }
      if (pos < size) {
         const unsigned nib = ((input[pos] & 1) << 3)
            | ((input[pos + 1] & 1) << 2)
            | ((input[pos + 2] & 1) << 1)
            | (input[pos + 3] & 1);
         result[out] = (uint8_t)hex_digits[nib];
      }
   }
   else
      __to_hstring_slow(input, size, is_signed, result, res_len);

   args[0].pointer = result;
   args[1].integer = 1;
   args[2].integer = res_len;
}

static void std_to_hstring(jit_func_t *func, jit_anchor_t *anchor,
                           jit_scalar_t *args, tlab_t *tlab)
{
   __do_to_hstring_swar(func, anchor, args, tlab, false);
}

static void ieee_to_hstring_signed(jit_func_t *func, jit_anchor_t *anchor,
                                   jit_scalar_t *args, tlab_t *tlab)
{
   __do_to_hstring_swar(func, anchor, args, tlab, true);
}

#ifdef HAVE_AVX2
__attribute__((target("avx2")))
static void __do_to_hstring_avx2(jit_func_t *func, jit_anchor_t *anchor,
                                 jit_scalar_t *args, tlab_t *tlab,
                                 bool is_signed)
{
   const int size = ffi_array_length(args[3].integer);
   const uint8_t *input = args[1].pointer;
   if (unlikely(size == 0))
      fail_in_interpreter(func, anchor, args, tlab);

   const int res_len = (size + 3) / 4;
   uint8_t *result = __tlab_alloc(tlab, res_len, 8);

   if (likely((size & 3) == 0 && __all_01_avx2(input, size))) {
      int pos = 0, out = 0;
      for (; pos + 32 <= size; pos += 32) {
         const uint32_t w = __pack32_avx2(
            _mm256_loadu_si256((const __m256i *)(input + pos)));
         result[out + 0] = (uint8_t)hex_digits[(w >> 28) & 0xf];
         result[out + 1] = (uint8_t)hex_digits[(w >> 24) & 0xf];
         result[out + 2] = (uint8_t)hex_digits[(w >> 20) & 0xf];
         result[out + 3] = (uint8_t)hex_digits[(w >> 16) & 0xf];
         result[out + 4] = (uint8_t)hex_digits[(w >> 12) & 0xf];
         result[out + 5] = (uint8_t)hex_digits[(w >>  8) & 0xf];
         result[out + 6] = (uint8_t)hex_digits[(w >>  4) & 0xf];
         result[out + 7] = (uint8_t)hex_digits[(w >>  0) & 0xf];
         out += 8;
      }
      for (; pos + 8 <= size; pos += 8) {
         const uint8_t b = __pack_low_bits(input + pos);
         result[out++] = (uint8_t)hex_digits[b >> 4];
         result[out++] = (uint8_t)hex_digits[b & 0xf];
      }
      if (pos < size) {
         const unsigned nib = ((input[pos] & 1) << 3)
            | ((input[pos + 1] & 1) << 2)
            | ((input[pos + 2] & 1) << 1)
            | (input[pos + 3] & 1);
         result[out] = (uint8_t)hex_digits[nib];
      }
   }
   else
      __to_hstring_slow(input, size, is_signed, result, res_len);

   args[0].pointer = result;
   args[1].integer = 1;
   args[2].integer = res_len;
}

__attribute__((target("avx2")))
static void std_to_hstring_avx2(jit_func_t *func, jit_anchor_t *anchor,
                                jit_scalar_t *args, tlab_t *tlab)
{
   __do_to_hstring_avx2(func, anchor, args, tlab, false);
}

__attribute__((target("avx2")))
static void ieee_to_hstring_signed_avx2(jit_func_t *func, jit_anchor_t *anchor,
                                        jit_scalar_t *args, tlab_t *tlab)
{
   __do_to_hstring_avx2(func, anchor, args, tlab, true);
}
#endif

static void std_to_ostring(jit_func_t *func, jit_anchor_t *anchor,
                           jit_scalar_t *args, tlab_t *tlab)
{
   const int size = ffi_array_length(args[3].integer);
   const uint8_t *input = args[1].pointer;
   if (unlikely(size == 0))
      fail_in_interpreter(func, anchor, args, tlab);

   const int res_len = (size + 2) / 3;
   uint8_t *result = __tlab_alloc(tlab, res_len, 8);
   __to_ostring_slow(input, size, false, result, res_len);

   args[0].pointer = result;
   args[1].integer = 1;
   args[2].integer = res_len;
}

static void ieee_to_ostring_signed(jit_func_t *func, jit_anchor_t *anchor,
                                   jit_scalar_t *args, tlab_t *tlab)
{
   const int size = ffi_array_length(args[3].integer);
   const uint8_t *input = args[1].pointer;
   if (unlikely(size == 0))
      fail_in_interpreter(func, anchor, args, tlab);

   const int res_len = (size + 2) / 3;
   uint8_t *result = __tlab_alloc(tlab, res_len, 8);
   __to_ostring_slow(input, size, true, result, res_len);

   args[0].pointer = result;
   args[1].integer = 1;
   args[2].integer = res_len;
}

// Cold 9-state / wide-vector fallbacks for NUMERIC_STD arithmetic & comparisons

__attribute__((cold, noinline))
static void ieee_addsub_slow(jit_scalar_t *args, tlab_t *tlab,
                             bool is_signed, bool is_sub)
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
      return;
   }

   left = is_signed ? __resize_signed(tlab, left, lsize, size)
                    : __resize_unsigned(tlab, left, lsize, size);
   right = is_signed ? __resize_signed(tlab, right, rsize, size)
                     : __resize_unsigned(tlab, right, rsize, size);

   left = __to_01(tlab, left, size, _X);
   right = __to_01(tlab, right, size, _X);

   if (left[0] == _X)
      args[0].pointer = left;
   else if (right[0] == _X)
      args[0].pointer = right;
   else {
      uint8_t *result = __tlab_alloc(tlab, size, 8);
      if (is_sub) {
         __invert_bits(right, size, result);
         (*ieee_packed_add)(left, result, size, 1, result);
      }
      else
         (*ieee_packed_add)(left, right, size, 0, result);
      args[0].pointer = result;
   }

   args[1].integer = size - 1;
   args[2].integer = ~size;
}

__attribute__((cold, noinline))
static void ieee_addsub_scalar_slow(jit_func_t *func, jit_anchor_t *anchor,
                                    jit_scalar_t *args, tlab_t *tlab,
                                    bool is_signed, bool is_sub,
                                    bool scalar_on_left)
{
   const int size = ffi_array_length(args[3].integer);
   uint8_t *vec = args[1].pointer;
   const int64_t imm = args[4].integer;

   if (size == 0) {
      args[0].pointer = NULL;
      args[1].integer = 0;
      args[2].integer = -1;
      return;
   }

   uint8_t *rhs = is_signed ? __to_signed(func, anchor, tlab, imm, size)
                            : __to_unsigned(func, anchor, tlab, (uint64_t)imm, size);

   vec = __to_01(tlab, vec, size, _X);

   if (vec[0] == _X)
      args[0].pointer = vec;
   else if (!is_sub && imm == 0)
      args[0].pointer = vec;
   else if (is_sub && !scalar_on_left && imm == 0)
      args[0].pointer = vec;
   else if (!is_sub) {
      (*ieee_packed_add)(vec, rhs, size, 0, rhs);
      args[0].pointer = rhs;
   }
   else if (!scalar_on_left) {
      __invert_bits(rhs, size, rhs);
      (*ieee_packed_add)(vec, rhs, size, 1, rhs);
      args[0].pointer = rhs;
   }
   else {
      uint8_t *inv = __tlab_alloc(tlab, size, 8);
      __invert_bits(vec, size, inv);
      (*ieee_packed_add)(rhs, inv, size, 1, rhs);
      args[0].pointer = rhs;
   }

   args[1].integer = size - 1;
   args[2].integer = ~size;
}

__attribute__((cold, noinline))
static void ieee_addsub_logic_slow(jit_scalar_t *args, tlab_t *tlab,
                                   bool is_signed, bool is_sub,
                                   bool scalar_on_left)
{
   const int size = ffi_array_length(args[3].integer);
   uint8_t *vec = args[1].pointer;
   const uint8_t bit = args[4].integer;

   if (size == 0) {
      args[0].pointer = NULL;
      args[1].integer = 0;
      args[2].integer = -1;
      return;
   }

   uint8_t *svec = __tlab_alloc(tlab, size, 8);
   memset(svec, _0, size);
   svec[size - 1] = bit;

   uint8_t *l = scalar_on_left ? svec : vec;
   uint8_t *r = scalar_on_left ? vec : svec;

   l = __to_01(tlab, l, size, _X);
   r = __to_01(tlab, r, size, _X);

   if (l[0] == _X)
      args[0].pointer = l;
   else if (r[0] == _X)
      args[0].pointer = r;
   else {
      uint8_t *result = __tlab_alloc(tlab, size, 8);
      if (is_sub) {
         __invert_bits(r, size, result);
         (*ieee_packed_add)(l, result, size, 1, result);
      }
      else
         (*ieee_packed_add)(l, r, size, 0, result);
      args[0].pointer = result;
   }

   args[1].integer = size - 1;
   args[2].integer = ~size;
}

__attribute__((cold, noinline))
static void ieee_neg_abs_signed_slow(jit_scalar_t *args, tlab_t *tlab,
                                     bool is_abs)
{
   const int size = ffi_array_length(args[3].integer);
   uint8_t *arg = args[1].pointer;

   if (size == 0) {
      args[0].pointer = NULL;
      args[1].integer = 0;
      args[2].integer = -1;
      return;
   }

   arg = __to_01(tlab, arg, size, _X);
   if (arg[0] == _X || (is_abs && arg[0] == _0))
      args[0].pointer = arg;
   else {
      uint8_t *result = __tlab_alloc(tlab, size, 8);
      uint8_t *zeros = __tlab_alloc(tlab, size, 8);
      memset(zeros, _0, size);
      __invert_bits(arg, size, result);
      (*ieee_packed_add)(result, zeros, size, 1, result);
      args[0].pointer = result;
   }

   args[1].integer = size - 1;
   args[2].integer = ~size;
}

__attribute__((cold, noinline))
static void ieee_mul_slow(jit_scalar_t *args, tlab_t *tlab, bool is_signed)
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
      return;
   }

   uint8_t *result = __tlab_alloc(tlab, size, 8);
   const uint32_t mark = __tlab_mark(tlab);

   left = __to_01(tlab, left, lsize, _X);
   right = __to_01(tlab, right, rsize, _X);

   if (left[0] == _X || right[0] == _X)
      memset(result, _X, size);
   else if (lsize <= 64 && rsize <= 64) {
      uint64_t lv = __pack_to_u64(left, lsize);
      uint64_t rv = __pack_to_u64(right, rsize);
      if (size <= 64) {
         uint64_t prod = is_signed
            ? (uint64_t)(__sign_extend_u64(lv, lsize) * __sign_extend_u64(rv, rsize))
            : lv * rv;
         __unpack_from_u64(prod, result, size);
      }
      else {
         unsigned __int128 prod = is_signed
            ? (unsigned __int128)((__int128)__sign_extend_u64(lv, lsize)
                                  * (__int128)__sign_extend_u64(rv, rsize))
            : (unsigned __int128)lv * (unsigned __int128)rv;
         const int hi = size - 64;
         __unpack_from_u64((uint64_t)(prod >> 64), result, hi);
         __unpack_from_u64((uint64_t)prod, result + hi, 64);
      }
   }
   else if (is_signed)
      __mul_signed(left, lsize, right, rsize, tlab, result);
   else
      __mul_unsigned(left, lsize, right, rsize, tlab, result);

   __tlab_restore(tlab, mark);

   args[0].pointer = result;
   args[1].integer = size - 1;
   args[2].integer = ~size;
}

typedef enum {
   CMP_EQ,
   CMP_NEQ,
   CMP_LT,
   CMP_LEQ,
   CMP_GT,
   CMP_GEQ,
} ieee_cmp_op_t;

static const char *const ieee_cmp_names[] = {
   "=", "/=", "<", "<=", ">", ">="
};

__attribute__((always_inline))
static inline int __eval_cmp_i(int cmp, ieee_cmp_op_t op)
{
   switch (op) {
   case CMP_EQ:  return cmp == 0;
   case CMP_NEQ: return cmp != 0;
   case CMP_LT:  return cmp < 0;
   case CMP_LEQ: return cmp <= 0;
   case CMP_GT:  return cmp > 0;
   case CMP_GEQ: return cmp >= 0;
   }
   return 0;
}

__attribute__((cold, noinline))
static void ieee_cmp_vec_slow(jit_func_t *func, jit_anchor_t *anchor,
                              jit_scalar_t *args, tlab_t *tlab,
                              bool is_signed, ieee_cmp_op_t op)
{
   const int lsize = ffi_array_length(args[3].integer);
   const int rsize = ffi_array_length(args[6].integer);
   uint8_t *left = args[1].pointer;
   uint8_t *right = args[4].pointer;
   const bool def = (op == CMP_NEQ);

   args[0].integer = def;

   if (lsize < 1 || rsize < 1) {
      ieee_warn(func, anchor, "NUMERIC_STD.\"%s\": null argument detected, "
                "returning %s", ieee_cmp_names[op], def ? "TRUE" : "FALSE");
      return;
   }

   const uint32_t mark = __tlab_mark(tlab);

   left = __to_01(tlab, left, lsize, _X);
   right = __to_01(tlab, right, rsize, _X);

   if (left[0] == _X || right[0] == _X) {
      ieee_warn(func, anchor, "NUMERIC_STD.\"%s\": metavalue detected, "
                "returning %s", ieee_cmp_names[op], def ? "TRUE" : "FALSE");
      __tlab_restore(tlab, mark);
      return;
   }

   const int size = MAX(lsize, rsize);
   left = is_signed ? __resize_signed(tlab, left, lsize, size)
                    : __resize_unsigned(tlab, left, lsize, size);
   right = is_signed ? __resize_signed(tlab, right, rsize, size)
                     : __resize_unsigned(tlab, right, rsize, size);

   int cmp;
   if (is_signed && left[0] != right[0])
      cmp = (left[0] == _1) ? -1 : 1;
   else
      cmp = memcmp(left, right, size);

   args[0].integer = __eval_cmp_i(cmp, op);
   __tlab_restore(tlab, mark);
}

__attribute__((cold, noinline))
static void ieee_cmp_scalar_slow(jit_func_t *func, jit_anchor_t *anchor,
                                 jit_scalar_t *args, tlab_t *tlab,
                                 bool is_signed, bool scalar_on_left,
                                 ieee_cmp_op_t op)
{
   const int size = ffi_array_length(args[3].integer);
   uint8_t *vec = args[1].pointer;
   const int64_t imm = args[4].integer;
   const bool def = (op == CMP_NEQ);

   args[0].integer = def;

   if (size < 1) {
      ieee_warn(func, anchor, "NUMERIC_STD.\"%s\": null argument detected, "
                "returning %s", ieee_cmp_names[op], def ? "TRUE" : "FALSE");
      return;
   }

   const uint32_t mark = __tlab_mark(tlab);
   vec = __to_01(tlab, vec, size, _X);

   if (vec[0] == _X) {
      ieee_warn(func, anchor, "NUMERIC_STD.\"%s\": metavalue detected, "
                "returning %s", ieee_cmp_names[op], def ? "TRUE" : "FALSE");
      __tlab_restore(tlab, mark);
      return;
   }

   // Compute cmp = sign(vec - imm)
   int cmp;
   if (size <= 64) {
      const uint64_t v = __pack_to_u64(vec, size);
      if (is_signed) {
         const int64_t sv = __sign_extend_u64(v, size);
         cmp = (sv < imm) ? -1 : (sv > imm ? 1 : 0);
      }
      else {
         const uint64_t uimm = (uint64_t)imm;
         cmp = (v < uimm) ? -1 : (v > uimm ? 1 : 0);
      }
   }
   else if (!is_signed) {
      bool hi_nz = false;
      for (int i = 0; i < size - 64; i++) {
         if (vec[i] == _1) {
            hi_nz = true;
            break;
         }
      }
      if (hi_nz)
         cmp = 1;
      else {
         const uint64_t lo = __pack_to_u64(vec + size - 64, 64);
         const uint64_t uimm = (uint64_t)imm;
         cmp = (lo < uimm) ? -1 : (lo > uimm ? 1 : 0);
      }
   }
   else {
      const uint8_t sign = vec[0];
      bool overflow = false;
      for (int i = 1; i <= size - 64; i++) {
         if (vec[i] != sign) {
            overflow = true;
            break;
         }
      }
      if (overflow)
         cmp = (sign == _1) ? -1 : 1;
      else {
         const int64_t lo = (int64_t)__pack_to_u64(vec + size - 64, 64);
         cmp = (lo < imm) ? -1 : (lo > imm ? 1 : 0);
      }
   }

   if (scalar_on_left)
      cmp = -cmp;

   args[0].integer = __eval_cmp_i(cmp, op);
   __tlab_restore(tlab, mark);
}

__attribute__((cold, noinline))
static void ieee_to_integer_unsigned_slow(jit_func_t *func,
                                          jit_anchor_t *anchor,
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

   if (size > 63) {
      for (int i = 0; i < size - 63; i++) {
         if (arg[i] != _0)
            fail_in_interpreter(func, anchor, args, tlab);
      }
   }

   const uint64_t val = (size > 64)
      ? __pack_to_u64(arg + size - 64, 64)
      : __pack_to_u64(arg, size);

   if (size > 31 && (val & UINT64_C(0xffffffff80000000))
       && standard() < STD_19)
      fail_in_interpreter(func, anchor, args, tlab);

   args[0].integer = val;
   __tlab_restore(tlab, mark);
}

__attribute__((cold, noinline))
static void ieee_to_integer_signed_slow(jit_func_t *func,
                                        jit_anchor_t *anchor,
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
      for (int i = 1; i <= size - intsize; i++) {
         if (arg[i] != arg[0])
            fail_in_interpreter(func, anchor, args, tlab);
      }
   }

   const uint64_t raw = (size > 64)
      ? __pack_to_u64(arg + size - 64, 64)
      : __pack_to_u64(arg, size);
   args[0].integer = __sign_extend_u64(raw, MIN(size, 64));
   __tlab_restore(tlab, mark);
}

__attribute__((cold, noinline))
static void ieee_match_eq_slow(jit_func_t *func, jit_anchor_t *anchor,
                               jit_scalar_t *args, tlab_t *tlab,
                               bool is_signed, bool is_neq)
{
   const int lsize = ffi_array_length(args[3].integer);
   const int rsize = ffi_array_length(args[6].integer);
   const uint8_t *left = args[1].pointer;
   const uint8_t *right = args[4].pointer;

   if (lsize < 1 || rsize < 1) {
      ieee_warn(func, anchor, "NUMERIC_STD.\"%s\": null detected, returning X",
                is_neq ? "?/=" : "?=");
      args[0].integer = _X;
      return;
   }

   const uint32_t mark = __tlab_mark(tlab);
   const int size = MAX(lsize, rsize);
   left = is_signed ? __resize_signed(tlab, left, lsize, size)
                    : __resize_unsigned(tlab, left, lsize, size);
   right = is_signed ? __resize_signed(tlab, right, rsize, size)
                     : __resize_unsigned(tlab, right, rsize, size);

   uint8_t result = is_neq ? _0 : _1;
   for (int i = size - 1; i >= 0; i--) {
      uint8_t r1 = match_eq_table[left[i]][right[i]];
      if (is_neq)
         r1 = not_table[r1];
      if (r1 == _U) {
         __tlab_restore(tlab, mark);
         args[0].integer = _U;
         return;
      }
      else if (r1 == _X || result == _X)
         result = _X;
      else
         result = is_neq ? or_table[result][r1] : and_table[result][r1];
   }

   __tlab_restore(tlab, mark);
   args[0].integer = result;
}

__attribute__((cold, noinline))
static void ieee_match_eq_scalar_slow(jit_func_t *func, jit_anchor_t *anchor,
                                      jit_scalar_t *args, tlab_t *tlab,
                                      bool is_signed, bool is_neq)
{
   const int size = ffi_array_length(args[3].integer);
   const uint8_t *vec = args[1].pointer;
   const int64_t imm = args[4].integer;

   if (size < 1) {
      ieee_warn(func, anchor, "NUMERIC_STD.\"%s\": null detected, returning X",
                is_neq ? "?/=" : "?=");
      args[0].integer = _X;
      return;
   }

   const uint32_t mark = __tlab_mark(tlab);
   const uint8_t *rhs = is_signed
      ? __to_signed(func, anchor, tlab, imm, size)
      : __to_unsigned(func, anchor, tlab, (uint64_t)imm, size);

   uint8_t result = is_neq ? _0 : _1;
   for (int i = size - 1; i >= 0; i--) {
      uint8_t r1 = match_eq_table[vec[i]][rhs[i]];
      if (is_neq)
         r1 = not_table[r1];
      if (r1 == _U) {
         __tlab_restore(tlab, mark);
         args[0].integer = _U;
         return;
      }
      else if (r1 == _X || result == _X)
         result = _X;
      else
         result = is_neq ? or_table[result][r1] : and_table[result][r1];
   }

   __tlab_restore(tlab, mark);
   args[0].integer = result;
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

// ISA-specialized NUMERIC_STD fast paths (SWAR, SSE4.1, AVX2)
#define DEFINE_NUMERIC_ISA(SFX, ATTR, TRY_PACK, UNPACK, ALIGN)          \
ATTR                                                                    \
static void ieee_plus_unsigned##SFX(jit_func_t *func,                   \
                                    jit_anchor_t *anchor,               \
                                    jit_scalar_t *args, tlab_t *tlab)   \
{                                                                       \
   const int lsize = ffi_array_length(args[3].integer);                 \
   const int rsize = ffi_array_length(args[6].integer);                 \
   const int size = MAX(lsize, rsize);                                  \
   if (likely(lsize >= 1 && rsize >= 1 && size <= 64)) {                \
      uint64_t lv, rv;                                                  \
      if (likely(TRY_PACK(args[1].pointer, lsize, &lv)                  \
                 && TRY_PACK(args[4].pointer, rsize, &rv))) {           \
         uint8_t *res = __tlab_alloc(tlab, ALIGN_UP(size, ALIGN), 16);  \
         UNPACK(lv + rv, res, size);                                    \
         args[0].pointer = res;                                         \
         args[1].integer = size - 1;                                    \
         args[2].integer = ~size;                                       \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_addsub_slow(args, tlab, false, false);                          \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_plus_signed##SFX(jit_func_t *func,                     \
                                  jit_anchor_t *anchor,                 \
                                  jit_scalar_t *args, tlab_t *tlab)     \
{                                                                       \
   const int lsize = ffi_array_length(args[3].integer);                 \
   const int rsize = ffi_array_length(args[6].integer);                 \
   const int size = MAX(lsize, rsize);                                  \
   if (likely(lsize >= 1 && rsize >= 1 && size <= 64)) {                \
      uint64_t lv, rv;                                                  \
      if (likely(TRY_PACK(args[1].pointer, lsize, &lv)                  \
                 && TRY_PACK(args[4].pointer, rsize, &rv))) {           \
         const uint64_t ls = (uint64_t)__sign_extend_u64(lv, lsize);    \
         const uint64_t rs = (uint64_t)__sign_extend_u64(rv, rsize);    \
         uint8_t *res = __tlab_alloc(tlab, ALIGN_UP(size, ALIGN), 16);  \
         UNPACK(ls + rs, res, size);                                    \
         args[0].pointer = res;                                         \
         args[1].integer = size - 1;                                    \
         args[2].integer = ~size;                                       \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_addsub_slow(args, tlab, true, false);                           \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_minus_unsigned##SFX(jit_func_t *func,                  \
                                     jit_anchor_t *anchor,              \
                                     jit_scalar_t *args, tlab_t *tlab)  \
{                                                                       \
   const int lsize = ffi_array_length(args[3].integer);                 \
   const int rsize = ffi_array_length(args[6].integer);                 \
   const int size = MAX(lsize, rsize);                                  \
   if (likely(lsize >= 1 && rsize >= 1 && size <= 64)) {                \
      uint64_t lv, rv;                                                  \
      if (likely(TRY_PACK(args[1].pointer, lsize, &lv)                  \
                 && TRY_PACK(args[4].pointer, rsize, &rv))) {           \
         uint8_t *res = __tlab_alloc(tlab, ALIGN_UP(size, ALIGN), 16);  \
         UNPACK(lv - rv, res, size);                                    \
         args[0].pointer = res;                                         \
         args[1].integer = size - 1;                                    \
         args[2].integer = ~size;                                       \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_addsub_slow(args, tlab, false, true);                           \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_minus_signed##SFX(jit_func_t *func,                    \
                                   jit_anchor_t *anchor,                \
                                   jit_scalar_t *args, tlab_t *tlab)    \
{                                                                       \
   const int lsize = ffi_array_length(args[3].integer);                 \
   const int rsize = ffi_array_length(args[6].integer);                 \
   const int size = MAX(lsize, rsize);                                  \
   if (likely(lsize >= 1 && rsize >= 1 && size <= 64)) {                \
      uint64_t lv, rv;                                                  \
      if (likely(TRY_PACK(args[1].pointer, lsize, &lv)                  \
                 && TRY_PACK(args[4].pointer, rsize, &rv))) {           \
         const uint64_t ls = (uint64_t)__sign_extend_u64(lv, lsize);    \
         const uint64_t rs = (uint64_t)__sign_extend_u64(rv, rsize);    \
         uint8_t *res = __tlab_alloc(tlab, ALIGN_UP(size, ALIGN), 16);  \
         UNPACK(ls - rs, res, size);                                    \
         args[0].pointer = res;                                         \
         args[1].integer = size - 1;                                    \
         args[2].integer = ~size;                                       \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_addsub_slow(args, tlab, true, true);                            \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_plus_unsigned_natural##SFX(jit_func_t *func,           \
                                            jit_anchor_t *anchor,       \
                                            jit_scalar_t *args,         \
                                            tlab_t *tlab)               \
{                                                                       \
   const int size = ffi_array_length(args[3].integer);                  \
   const uint64_t right = (uint64_t)args[4].integer;                    \
   if (likely(size >= 1 && size <= 64)) {                               \
      uint64_t lv;                                                      \
      if (likely(TRY_PACK(args[1].pointer, size, &lv))) {               \
         __check_to_unsigned_trunc(func, anchor, right, size);          \
         if (right == 0)                                                \
            args[0].pointer = args[1].pointer;                          \
         else {                                                         \
            uint8_t *res = __tlab_alloc(tlab, ALIGN_UP(size, ALIGN), 16); \
            UNPACK(lv + right, res, size);                              \
            args[0].pointer = res;                                      \
         }                                                              \
         args[1].integer = size - 1;                                    \
         args[2].integer = ~size;                                       \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_addsub_scalar_slow(func, anchor, args, tlab, false, false, false); \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_plus_natural_unsigned##SFX(jit_func_t *func,           \
                                            jit_anchor_t *anchor,       \
                                            jit_scalar_t *args,         \
                                            tlab_t *tlab)               \
{                                                                       \
   __commute_vector_scalar(args);                                       \
   ieee_plus_unsigned_natural##SFX(func, anchor, args, tlab);           \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_minus_unsigned_natural##SFX(jit_func_t *func,          \
                                             jit_anchor_t *anchor,      \
                                             jit_scalar_t *args,        \
                                             tlab_t *tlab)              \
{                                                                       \
   const int size = ffi_array_length(args[3].integer);                  \
   const uint64_t right = (uint64_t)args[4].integer;                    \
   if (likely(size >= 1 && size <= 64)) {                               \
      uint64_t lv;                                                      \
      if (likely(TRY_PACK(args[1].pointer, size, &lv))) {               \
         __check_to_unsigned_trunc(func, anchor, right, size);          \
         if (right == 0)                                                \
            args[0].pointer = args[1].pointer;                          \
         else {                                                         \
            uint8_t *res = __tlab_alloc(tlab, ALIGN_UP(size, ALIGN), 16); \
            UNPACK(lv - right, res, size);                              \
            args[0].pointer = res;                                      \
         }                                                              \
         args[1].integer = size - 1;                                    \
         args[2].integer = ~size;                                       \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_addsub_scalar_slow(func, anchor, args, tlab, false, true, false); \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_minus_natural_unsigned##SFX(jit_func_t *func,          \
                                             jit_anchor_t *anchor,      \
                                             jit_scalar_t *args,        \
                                             tlab_t *tlab)              \
{                                                                       \
   __commute_vector_scalar(args);                                       \
   const int size = ffi_array_length(args[3].integer);                  \
   const uint64_t left = (uint64_t)args[4].integer;                     \
   if (likely(size >= 1 && size <= 64)) {                               \
      uint64_t rv;                                                      \
      if (likely(TRY_PACK(args[1].pointer, size, &rv))) {               \
         __check_to_unsigned_trunc(func, anchor, left, size);           \
         uint8_t *res = __tlab_alloc(tlab, ALIGN_UP(size, ALIGN), 16);  \
         UNPACK(left - rv, res, size);                                  \
         args[0].pointer = res;                                         \
         args[1].integer = size - 1;                                    \
         args[2].integer = ~size;                                       \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_addsub_scalar_slow(func, anchor, args, tlab, false, true, true); \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_plus_signed_integer##SFX(jit_func_t *func,             \
                                          jit_anchor_t *anchor,         \
                                          jit_scalar_t *args,           \
                                          tlab_t *tlab)                 \
{                                                                       \
   const int size = ffi_array_length(args[3].integer);                  \
   const int64_t right = args[4].integer;                               \
   if (likely(size >= 1 && size <= 64)) {                               \
      uint64_t lv;                                                      \
      if (likely(TRY_PACK(args[1].pointer, size, &lv))) {               \
         __check_to_signed_trunc(func, anchor, right, size);            \
         if (right == 0)                                                \
            args[0].pointer = args[1].pointer;                          \
         else {                                                         \
            uint8_t *res = __tlab_alloc(tlab, ALIGN_UP(size, ALIGN), 16); \
            UNPACK(lv + (uint64_t)right, res, size);                    \
            args[0].pointer = res;                                      \
         }                                                              \
         args[1].integer = size - 1;                                    \
         args[2].integer = ~size;                                       \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_addsub_scalar_slow(func, anchor, args, tlab, true, false, false); \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_plus_integer_signed##SFX(jit_func_t *func,             \
                                          jit_anchor_t *anchor,         \
                                          jit_scalar_t *args,           \
                                          tlab_t *tlab)                 \
{                                                                       \
   __commute_vector_scalar(args);                                       \
   ieee_plus_signed_integer##SFX(func, anchor, args, tlab);             \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_minus_signed_integer##SFX(jit_func_t *func,            \
                                           jit_anchor_t *anchor,        \
                                           jit_scalar_t *args,          \
                                           tlab_t *tlab)                \
{                                                                       \
   const int size = ffi_array_length(args[3].integer);                  \
   const int64_t right = args[4].integer;                               \
   if (likely(size >= 1 && size <= 64)) {                               \
      uint64_t lv;                                                      \
      if (likely(TRY_PACK(args[1].pointer, size, &lv))) {               \
         __check_to_signed_trunc(func, anchor, right, size);            \
         if (right == 0)                                                \
            args[0].pointer = args[1].pointer;                          \
         else {                                                         \
            uint8_t *res = __tlab_alloc(tlab, ALIGN_UP(size, ALIGN), 16); \
            UNPACK(lv - (uint64_t)right, res, size);                    \
            args[0].pointer = res;                                      \
         }                                                              \
         args[1].integer = size - 1;                                    \
         args[2].integer = ~size;                                       \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_addsub_scalar_slow(func, anchor, args, tlab, true, true, false); \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_minus_integer_signed##SFX(jit_func_t *func,            \
                                           jit_anchor_t *anchor,        \
                                           jit_scalar_t *args,          \
                                           tlab_t *tlab)                \
{                                                                       \
   __commute_vector_scalar(args);                                       \
   const int size = ffi_array_length(args[3].integer);                  \
   const int64_t left = args[4].integer;                                \
   if (likely(size >= 1 && size <= 64)) {                               \
      uint64_t rv;                                                      \
      if (likely(TRY_PACK(args[1].pointer, size, &rv))) {               \
         __check_to_signed_trunc(func, anchor, left, size);             \
         uint8_t *res = __tlab_alloc(tlab, ALIGN_UP(size, ALIGN), 16);  \
         UNPACK((uint64_t)left - rv, res, size);                        \
         args[0].pointer = res;                                         \
         args[1].integer = size - 1;                                    \
         args[2].integer = ~size;                                       \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_addsub_scalar_slow(func, anchor, args, tlab, true, true, true); \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_plus_vec_logic##SFX(jit_func_t *func,                  \
                                     jit_anchor_t *anchor,              \
                                     jit_scalar_t *args, tlab_t *tlab)  \
{                                                                       \
   const int size = ffi_array_length(args[3].integer);                  \
   const uint8_t bit = args[4].integer;                                 \
   if (likely(size >= 1 && size <= 64 && IS_01(bit))) {                 \
      uint64_t lv;                                                      \
      if (likely(TRY_PACK(args[1].pointer, size, &lv))) {               \
         if (bit == _0)                                                 \
            args[0].pointer = args[1].pointer;                          \
         else {                                                         \
            uint8_t *res = __tlab_alloc(tlab, ALIGN_UP(size, ALIGN), 16); \
            UNPACK(lv + 1, res, size);                                  \
            args[0].pointer = res;                                      \
         }                                                              \
         args[1].integer = size - 1;                                    \
         args[2].integer = ~size;                                       \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_addsub_logic_slow(args, tlab, false, false, false);             \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_plus_logic_vec##SFX(jit_func_t *func,                  \
                                     jit_anchor_t *anchor,              \
                                     jit_scalar_t *args, tlab_t *tlab)  \
{                                                                       \
   __commute_vector_scalar(args);                                       \
   ieee_plus_vec_logic##SFX(func, anchor, args, tlab);                  \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_minus_vec_logic##SFX(jit_func_t *func,                 \
                                      jit_anchor_t *anchor,             \
                                      jit_scalar_t *args, tlab_t *tlab) \
{                                                                       \
   const int size = ffi_array_length(args[3].integer);                  \
   const uint8_t bit = args[4].integer;                                 \
   if (likely(size >= 1 && size <= 64 && IS_01(bit))) {                 \
      uint64_t lv;                                                      \
      if (likely(TRY_PACK(args[1].pointer, size, &lv))) {               \
         if (bit == _0)                                                 \
            args[0].pointer = args[1].pointer;                          \
         else {                                                         \
            uint8_t *res = __tlab_alloc(tlab, ALIGN_UP(size, ALIGN), 16); \
            UNPACK(lv - 1, res, size);                                  \
            args[0].pointer = res;                                      \
         }                                                              \
         args[1].integer = size - 1;                                    \
         args[2].integer = ~size;                                       \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_addsub_logic_slow(args, tlab, false, true, false);              \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_minus_logic_vec##SFX(jit_func_t *func,                 \
                                      jit_anchor_t *anchor,             \
                                      jit_scalar_t *args, tlab_t *tlab) \
{                                                                       \
   __commute_vector_scalar(args);                                       \
   const int size = ffi_array_length(args[3].integer);                  \
   const uint8_t bit = args[4].integer;                                 \
   if (likely(size >= 1 && size <= 64 && IS_01(bit))) {                 \
      uint64_t rv;                                                      \
      if (likely(TRY_PACK(args[1].pointer, size, &rv))) {               \
         uint8_t *res = __tlab_alloc(tlab, ALIGN_UP(size, ALIGN), 16);  \
         UNPACK((uint64_t)(bit & 1) - rv, res, size);                   \
         args[0].pointer = res;                                         \
         args[1].integer = size - 1;                                    \
         args[2].integer = ~size;                                       \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_addsub_logic_slow(args, tlab, false, true, true);               \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_neg_signed##SFX(jit_func_t *func,                      \
                                 jit_anchor_t *anchor,                  \
                                 jit_scalar_t *args, tlab_t *tlab)      \
{                                                                       \
   const int size = ffi_array_length(args[3].integer);                  \
   if (likely(size >= 1 && size <= 64)) {                               \
      uint64_t v;                                                       \
      if (likely(TRY_PACK(args[1].pointer, size, &v))) {                \
         uint8_t *res = __tlab_alloc(tlab, ALIGN_UP(size, ALIGN), 16);  \
         UNPACK(-v, res, size);                                         \
         args[0].pointer = res;                                         \
         args[1].integer = size - 1;                                    \
         args[2].integer = ~size;                                       \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_neg_abs_signed_slow(args, tlab, false);                         \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_abs_signed##SFX(jit_func_t *func,                      \
                                 jit_anchor_t *anchor,                  \
                                 jit_scalar_t *args, tlab_t *tlab)      \
{                                                                       \
   const int size = ffi_array_length(args[3].integer);                  \
   const uint8_t *arg = args[1].pointer;                                \
   if (likely(size >= 1 && size <= 64)) {                               \
      uint64_t v;                                                       \
      if (likely(TRY_PACK(arg, size, &v))) {                            \
         if (arg[0] == _0)                                              \
            args[0].pointer = (uint8_t *)arg;                           \
         else {                                                         \
            uint8_t *res = __tlab_alloc(tlab, ALIGN_UP(size, ALIGN), 16); \
            UNPACK(-v, res, size);                                      \
            args[0].pointer = res;                                      \
         }                                                              \
         args[1].integer = size - 1;                                    \
         args[2].integer = ~size;                                       \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_neg_abs_signed_slow(args, tlab, true);                          \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_mul_unsigned##SFX(jit_func_t *func,                    \
                                   jit_anchor_t *anchor,                \
                                   jit_scalar_t *args, tlab_t *tlab)    \
{                                                                       \
   const int lsize = ffi_array_length(args[3].integer);                 \
   const int rsize = ffi_array_length(args[6].integer);                 \
   if (likely(lsize >= 1 && rsize >= 1 && lsize <= 64 && rsize <= 64)) {\
      uint64_t lv, rv;                                                  \
      if (likely(TRY_PACK(args[1].pointer, lsize, &lv)                  \
                 && TRY_PACK(args[4].pointer, rsize, &rv))) {           \
         const int size = lsize + rsize;                                \
         uint8_t *res = __tlab_alloc(tlab, ALIGN_UP(size, ALIGN), 16);  \
         if (size <= 64)                                                \
            UNPACK(lv * rv, res, size);                                 \
         else {                                                         \
            unsigned __int128 prod =                                    \
               (unsigned __int128)lv * (unsigned __int128)rv;           \
            const int hi = size - 64;                                   \
            UNPACK((uint64_t)(prod >> 64), res, hi);                    \
            UNPACK((uint64_t)prod, res + hi, 64);                       \
         }                                                              \
         args[0].pointer = res;                                         \
         args[1].integer = size - 1;                                    \
         args[2].integer = ~size;                                       \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_mul_slow(args, tlab, false);                                    \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_mul_signed##SFX(jit_func_t *func,                      \
                                 jit_anchor_t *anchor,                  \
                                 jit_scalar_t *args, tlab_t *tlab)      \
{                                                                       \
   const int lsize = ffi_array_length(args[3].integer);                 \
   const int rsize = ffi_array_length(args[6].integer);                 \
   if (likely(lsize >= 1 && rsize >= 1 && lsize <= 64 && rsize <= 64)) {\
      uint64_t lv, rv;                                                  \
      if (likely(TRY_PACK(args[1].pointer, lsize, &lv)                  \
                 && TRY_PACK(args[4].pointer, rsize, &rv))) {           \
         const int size = lsize + rsize;                                \
         const int64_t ls = __sign_extend_u64(lv, lsize);               \
         const int64_t rs = __sign_extend_u64(rv, rsize);               \
         uint8_t *res = __tlab_alloc(tlab, ALIGN_UP(size, ALIGN), 16);  \
         if (size <= 64)                                                \
            UNPACK((uint64_t)(ls * rs), res, size);                     \
         else {                                                         \
            unsigned __int128 prod =                                    \
               (unsigned __int128)((__int128)ls * (__int128)rs);        \
            const int hi = size - 64;                                   \
            UNPACK((uint64_t)(prod >> 64), res, hi);                    \
            UNPACK((uint64_t)prod, res + hi, 64);                       \
         }                                                              \
         args[0].pointer = res;                                         \
         args[1].integer = size - 1;                                    \
         args[2].integer = ~size;                                       \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_mul_slow(args, tlab, true);                                     \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_to_integer_unsigned##SFX(jit_func_t *func,             \
                                          jit_anchor_t *anchor,         \
                                          jit_scalar_t *args,           \
                                          tlab_t *tlab)                 \
{                                                                       \
   const int size = ffi_array_length(args[3].integer);                  \
   if (likely(size >= 1 && size <= 64)) {                               \
      uint64_t val;                                                     \
      if (likely(TRY_PACK(args[1].pointer, size, &val))) {              \
         if (unlikely(size > 31)) {                                     \
            if (unlikely((val >> 63) != 0))                             \
               fail_in_interpreter(func, anchor, args, tlab);           \
            if (unlikely((val & UINT64_C(0xffffffff80000000))           \
                         && standard() < STD_19))                       \
               fail_in_interpreter(func, anchor, args, tlab);           \
         }                                                              \
         args[0].integer = val;                                         \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_to_integer_unsigned_slow(func, anchor, args, tlab);             \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_to_integer_signed##SFX(jit_func_t *func,               \
                                        jit_anchor_t *anchor,           \
                                        jit_scalar_t *args,             \
                                        tlab_t *tlab)                   \
{                                                                       \
   const int size = ffi_array_length(args[3].integer);                  \
   if (likely(size >= 1 && size <= 64)) {                               \
      uint64_t val;                                                     \
      if (likely(TRY_PACK(args[1].pointer, size, &val))) {              \
         const int64_t sval = __sign_extend_u64(val, size);             \
         if (unlikely(size > 32 && standard() < STD_19                  \
                      && (sval < INT32_MIN || sval > INT32_MAX)))       \
            fail_in_interpreter(func, anchor, args, tlab);              \
         args[0].integer = sval;                                        \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_to_integer_signed_slow(func, anchor, args, tlab);               \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_to_unsigned##SFX(jit_func_t *func,                     \
                                  jit_anchor_t *anchor,                 \
                                  jit_scalar_t *args, tlab_t *tlab)     \
{                                                                       \
   const int64_t size = args[2].integer;                                \
   const uint64_t arg = (uint64_t)args[1].integer;                      \
   if (unlikely(size < 1)) {                                            \
      args[0].pointer = NULL;                                           \
      args[1].integer = 0;                                              \
      args[2].integer = -1;                                             \
      return;                                                           \
   }                                                                    \
   __check_to_unsigned_trunc(func, anchor, arg, (int)size);             \
   uint8_t *res = __tlab_alloc(tlab, ALIGN_UP(size, ALIGN), 16);        \
   if (likely(size <= 64))                                              \
      UNPACK(arg, res, (int)size);                                      \
   else {                                                               \
      memset(res, _0, (size_t)(size - 64));                             \
      UNPACK(arg, res + (size - 64), 64);                               \
   }                                                                    \
   args[0].pointer = res;                                               \
   args[1].integer = size - 1;                                          \
   args[2].integer = ~size;                                             \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_to_unsigned_res##SFX(jit_func_t *func,                 \
                                      jit_anchor_t *anchor,             \
                                      jit_scalar_t *args, tlab_t *tlab) \
{                                                                       \
   args[2].integer = ffi_array_length(args[4].integer);                 \
   ieee_to_unsigned##SFX(func, anchor, args, tlab);                     \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_to_signed##SFX(jit_func_t *func,                       \
                                jit_anchor_t *anchor,                   \
                                jit_scalar_t *args, tlab_t *tlab)       \
{                                                                       \
   const int64_t size = args[2].integer;                                \
   const int64_t arg = args[1].integer;                                 \
   if (unlikely(size < 1)) {                                            \
      args[0].pointer = NULL;                                           \
      args[1].integer = 0;                                              \
      args[2].integer = -1;                                             \
      return;                                                           \
   }                                                                    \
   __check_to_signed_trunc(func, anchor, arg, (int)size);               \
   uint8_t *res = __tlab_alloc(tlab, ALIGN_UP(size, ALIGN), 16);        \
   if (likely(size <= 64))                                              \
      UNPACK((uint64_t)arg, res, (int)size);                            \
   else {                                                               \
      memset(res, arg < 0 ? _1 : _0, (size_t)(size - 64));              \
      UNPACK((uint64_t)arg, res + (size - 64), 64);                     \
   }                                                                    \
   args[0].pointer = res;                                               \
   args[1].integer = size - 1;                                          \
   args[2].integer = ~size;                                             \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_to_signed_res##SFX(jit_func_t *func,                   \
                                    jit_anchor_t *anchor,               \
                                    jit_scalar_t *args, tlab_t *tlab)   \
{                                                                       \
   args[2].integer = ffi_array_length(args[4].integer);                 \
   ieee_to_signed##SFX(func, anchor, args, tlab);                       \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_match_eq_unsigned##SFX(jit_func_t *func,               \
                                        jit_anchor_t *anchor,           \
                                        jit_scalar_t *args,             \
                                        tlab_t *tlab)                   \
{                                                                       \
   const int lsize = ffi_array_length(args[3].integer);                 \
   const int rsize = ffi_array_length(args[6].integer);                 \
   if (likely(lsize >= 1 && rsize >= 1 && lsize <= 64 && rsize <= 64)) {\
      uint64_t lv, rv;                                                  \
      if (likely(TRY_PACK(args[1].pointer, lsize, &lv)                  \
                 && TRY_PACK(args[4].pointer, rsize, &rv))) {           \
         args[0].integer = (lv == rv) ? _1 : _0;                        \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_match_eq_slow(func, anchor, args, tlab, false, false);          \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_match_neq_unsigned##SFX(jit_func_t *func,              \
                                         jit_anchor_t *anchor,          \
                                         jit_scalar_t *args,            \
                                         tlab_t *tlab)                  \
{                                                                       \
   const int lsize = ffi_array_length(args[3].integer);                 \
   const int rsize = ffi_array_length(args[6].integer);                 \
   if (likely(lsize >= 1 && rsize >= 1 && lsize <= 64 && rsize <= 64)) {\
      uint64_t lv, rv;                                                  \
      if (likely(TRY_PACK(args[1].pointer, lsize, &lv)                  \
                 && TRY_PACK(args[4].pointer, rsize, &rv))) {           \
         args[0].integer = (lv != rv) ? _1 : _0;                        \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_match_eq_slow(func, anchor, args, tlab, false, true);           \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_match_eq_signed##SFX(jit_func_t *func,                 \
                                      jit_anchor_t *anchor,             \
                                      jit_scalar_t *args, tlab_t *tlab) \
{                                                                       \
   const int lsize = ffi_array_length(args[3].integer);                 \
   const int rsize = ffi_array_length(args[6].integer);                 \
   if (likely(lsize >= 1 && rsize >= 1 && lsize <= 64 && rsize <= 64)) {\
      uint64_t lv, rv;                                                  \
      if (likely(TRY_PACK(args[1].pointer, lsize, &lv)                  \
                 && TRY_PACK(args[4].pointer, rsize, &rv))) {           \
         args[0].integer = (__sign_extend_u64(lv, lsize)                \
                            == __sign_extend_u64(rv, rsize)) ? _1 : _0; \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_match_eq_slow(func, anchor, args, tlab, true, false);           \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_match_neq_signed##SFX(jit_func_t *func,                \
                                       jit_anchor_t *anchor,            \
                                       jit_scalar_t *args,              \
                                       tlab_t *tlab)                    \
{                                                                       \
   const int lsize = ffi_array_length(args[3].integer);                 \
   const int rsize = ffi_array_length(args[6].integer);                 \
   if (likely(lsize >= 1 && rsize >= 1 && lsize <= 64 && rsize <= 64)) {\
      uint64_t lv, rv;                                                  \
      if (likely(TRY_PACK(args[1].pointer, lsize, &lv)                  \
                 && TRY_PACK(args[4].pointer, rsize, &rv))) {           \
         args[0].integer = (__sign_extend_u64(lv, lsize)                \
                            != __sign_extend_u64(rv, rsize)) ? _1 : _0; \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_match_eq_slow(func, anchor, args, tlab, true, true);            \
}

#define DEFINE_CMP_OP_ISA(SFX, ATTR, TRY_PACK, OPNAME, ENUM, COP)       \
ATTR                                                                    \
static void ieee_##OPNAME##_unsigned##SFX(jit_func_t *func,             \
                                          jit_anchor_t *anchor,         \
                                          jit_scalar_t *args,           \
                                          tlab_t *tlab)                 \
{                                                                       \
   const int lsize = ffi_array_length(args[3].integer);                 \
   const int rsize = ffi_array_length(args[6].integer);                 \
   if (likely(lsize >= 1 && rsize >= 1 && lsize <= 64 && rsize <= 64)) {\
      uint64_t lv, rv;                                                  \
      if (likely(TRY_PACK(args[1].pointer, lsize, &lv)                  \
                 && TRY_PACK(args[4].pointer, rsize, &rv))) {           \
         args[0].integer = (lv COP rv);                                 \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_cmp_vec_slow(func, anchor, args, tlab, false, ENUM);            \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_##OPNAME##_signed##SFX(jit_func_t *func,               \
                                        jit_anchor_t *anchor,           \
                                        jit_scalar_t *args,             \
                                        tlab_t *tlab)                   \
{                                                                       \
   const int lsize = ffi_array_length(args[3].integer);                 \
   const int rsize = ffi_array_length(args[6].integer);                 \
   if (likely(lsize >= 1 && rsize >= 1 && lsize <= 64 && rsize <= 64)) {\
      uint64_t lv, rv;                                                  \
      if (likely(TRY_PACK(args[1].pointer, lsize, &lv)                  \
                 && TRY_PACK(args[4].pointer, rsize, &rv))) {           \
         args[0].integer = (__sign_extend_u64(lv, lsize)                \
                            COP __sign_extend_u64(rv, rsize));          \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_cmp_vec_slow(func, anchor, args, tlab, true, ENUM);             \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_##OPNAME##_unat##SFX(jit_func_t *func,                 \
                                      jit_anchor_t *anchor,             \
                                      jit_scalar_t *args, tlab_t *tlab) \
{                                                                       \
   const int size = ffi_array_length(args[3].integer);                  \
   if (likely(size >= 1 && size <= 64)) {                               \
      uint64_t lv;                                                      \
      if (likely(TRY_PACK(args[1].pointer, size, &lv))) {               \
         args[0].integer = (lv COP (uint64_t)args[4].integer);          \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_cmp_scalar_slow(func, anchor, args, tlab, false, false, ENUM);  \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_##OPNAME##_natu##SFX(jit_func_t *func,                 \
                                      jit_anchor_t *anchor,             \
                                      jit_scalar_t *args, tlab_t *tlab) \
{                                                                       \
   __commute_vector_scalar(args);                                       \
   const int size = ffi_array_length(args[3].integer);                  \
   if (likely(size >= 1 && size <= 64)) {                               \
      uint64_t rv;                                                      \
      if (likely(TRY_PACK(args[1].pointer, size, &rv))) {               \
         args[0].integer = ((uint64_t)args[4].integer COP rv);          \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_cmp_scalar_slow(func, anchor, args, tlab, false, true, ENUM);   \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_##OPNAME##_sint##SFX(jit_func_t *func,                 \
                                      jit_anchor_t *anchor,             \
                                      jit_scalar_t *args, tlab_t *tlab) \
{                                                                       \
   const int size = ffi_array_length(args[3].integer);                  \
   if (likely(size >= 1 && size <= 64)) {                               \
      uint64_t lv;                                                      \
      if (likely(TRY_PACK(args[1].pointer, size, &lv))) {               \
         args[0].integer = (__sign_extend_u64(lv, size)                 \
                            COP args[4].integer);                       \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_cmp_scalar_slow(func, anchor, args, tlab, true, false, ENUM);   \
}                                                                       \
                                                                        \
ATTR                                                                    \
static void ieee_##OPNAME##_ints##SFX(jit_func_t *func,                 \
                                      jit_anchor_t *anchor,             \
                                      jit_scalar_t *args, tlab_t *tlab) \
{                                                                       \
   __commute_vector_scalar(args);                                       \
   const int size = ffi_array_length(args[3].integer);                  \
   if (likely(size >= 1 && size <= 64)) {                               \
      uint64_t rv;                                                      \
      if (likely(TRY_PACK(args[1].pointer, size, &rv))) {               \
         args[0].integer = (args[4].integer                             \
                            COP __sign_extend_u64(rv, size));           \
         return;                                                        \
      }                                                                 \
   }                                                                    \
   ieee_cmp_scalar_slow(func, anchor, args, tlab, true, true, ENUM);    \
}

#define DEFINE_ALL_NUMERIC_FOR_ISA(SFX, ATTR, TRY_PACK, UNPACK, ALIGN)  \
   DEFINE_NUMERIC_ISA(SFX, ATTR, TRY_PACK, UNPACK, ALIGN)               \
   DEFINE_CMP_OP_ISA(SFX, ATTR, TRY_PACK, eq,      CMP_EQ,  ==)         \
   DEFINE_CMP_OP_ISA(SFX, ATTR, TRY_PACK, neq,     CMP_NEQ, !=)         \
   DEFINE_CMP_OP_ISA(SFX, ATTR, TRY_PACK, less,    CMP_LT,  <)          \
   DEFINE_CMP_OP_ISA(SFX, ATTR, TRY_PACK, leq,     CMP_LEQ, <=)         \
   DEFINE_CMP_OP_ISA(SFX, ATTR, TRY_PACK, greater, CMP_GT,  >)          \
   DEFINE_CMP_OP_ISA(SFX, ATTR, TRY_PACK, geq,     CMP_GEQ, >=)

DEFINE_ALL_NUMERIC_FOR_ISA(, , __try_pack_u64_swar, __unpack_from_u64, 8)
#ifdef HAVE_SSE41
DEFINE_ALL_NUMERIC_FOR_ISA(_sse41, __attribute__((target("sse4.1"))),
                           __try_pack_u64_sse41, __unpack_from_u64_sse41, 16)
#endif
#ifdef HAVE_AVX2
DEFINE_ALL_NUMERIC_FOR_ISA(_avx2, __attribute__((target("avx2"))),
                           __try_pack_u64_avx2, __unpack_from_u64_avx2, 32)
#endif

static void ieee_match_eq_unat(jit_func_t *func, jit_anchor_t *anchor,
                               jit_scalar_t *args, tlab_t *tlab)
{
   ieee_match_eq_scalar_slow(func, anchor, args, tlab, false, false);
}

static void ieee_match_eq_natu(jit_func_t *func, jit_anchor_t *anchor,
                               jit_scalar_t *args, tlab_t *tlab)
{
   __commute_vector_scalar(args);
   ieee_match_eq_scalar_slow(func, anchor, args, tlab, false, false);
}

static void ieee_match_neq_unat(jit_func_t *func, jit_anchor_t *anchor,
                                jit_scalar_t *args, tlab_t *tlab)
{
   ieee_match_eq_scalar_slow(func, anchor, args, tlab, false, true);
}

static void ieee_match_neq_natu(jit_func_t *func, jit_anchor_t *anchor,
                                jit_scalar_t *args, tlab_t *tlab)
{
   __commute_vector_scalar(args);
   ieee_match_eq_scalar_slow(func, anchor, args, tlab, false, true);
}

static void ieee_match_eq_sint(jit_func_t *func, jit_anchor_t *anchor,
                               jit_scalar_t *args, tlab_t *tlab)
{
   ieee_match_eq_scalar_slow(func, anchor, args, tlab, true, false);
}

static void ieee_match_eq_ints(jit_func_t *func, jit_anchor_t *anchor,
                               jit_scalar_t *args, tlab_t *tlab)
{
   __commute_vector_scalar(args);
   ieee_match_eq_scalar_slow(func, anchor, args, tlab, true, false);
}

static void ieee_match_neq_sint(jit_func_t *func, jit_anchor_t *anchor,
                                jit_scalar_t *args, tlab_t *tlab)
{
   ieee_match_eq_scalar_slow(func, anchor, args, tlab, true, true);
}

static void ieee_match_neq_ints(jit_func_t *func, jit_anchor_t *anchor,
                                jit_scalar_t *args, tlab_t *tlab)
{
   __commute_vector_scalar(args);
   ieee_match_eq_scalar_slow(func, anchor, args, tlab, true, true);
}

// STD_MATCH (NUMERIC_STD) and STD_LOGIC_1164."?=" / "?/=" ($predef)
static void ieee_std_match_scalar(jit_func_t *func, jit_anchor_t *anchor,
                                  jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = (match_eq_table[args[1].integer][args[2].integer] == _1);
}

static void ieee_std_match_vec(jit_func_t *func, jit_anchor_t *anchor,
                               jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[3].integer);
   const int rsize = ffi_array_length(args[6].integer);
   const uint8_t *left = args[1].pointer;
   const uint8_t *right = args[4].pointer;

   if (unlikely(lsize < 1 || rsize < 1)) {
      ieee_warn(func, anchor,
                "NUMERIC_STD.STD_MATCH: null detected, returning FALSE");
      args[0].integer = 0;
      return;
   }
   if (unlikely(lsize != rsize)) {
      ieee_warn(func, anchor,
                "NUMERIC_STD.STD_MATCH: L'LENGTH /= R'LENGTH, returning FALSE");
      args[0].integer = 0;
      return;
   }

   int pos = 0;
   for (; pos + 8 <= lsize; pos += 8) {
      const uint64_t lw = unaligned_load(left + pos, uint64_t);
      const uint64_t rw = unaligned_load(right + pos, uint64_t);
      if (likely(IS_01(lw) && IS_01(rw))) {
         if (lw != rw) {
            args[0].integer = 0;
            return;
         }
      }
      else {
         for (int k = 0; k < 8; k++) {
            if (match_eq_table[left[pos + k]][right[pos + k]] != _1) {
               args[0].integer = 0;
               return;
            }
         }
      }
   }
   for (; pos < lsize; pos++) {
      if (match_eq_table[left[pos]][right[pos]] != _1) {
         args[0].integer = 0;
         return;
      }
   }

   args[0].integer = 1;
}

static void std_match_eq_scalar_predef(jit_func_t *func, jit_anchor_t *anchor,
                                       jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = match_eq_table[args[0].integer][args[1].integer];
}

static void std_match_neq_scalar_predef(jit_func_t *func, jit_anchor_t *anchor,
                                        jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = not_table[match_eq_table[args[0].integer][args[1].integer]];
}

static void std_match_eq_vec_predef(jit_func_t *func, jit_anchor_t *anchor,
                                    jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[2].integer);
   const int rsize = ffi_array_length(args[5].integer);
   const uint8_t *left = args[0].pointer;
   const uint8_t *right = args[3].pointer;

   if (unlikely(lsize != rsize))
      fail_in_interpreter(func, anchor, args, tlab);

   if (likely(__all_01(left, lsize) && __all_01(right, rsize))) {
      args[0].integer = (lsize == 0 || memcmp(left, right, lsize) == 0) ? _1 : _0;
      return;
   }

   uint8_t cur = _1;
   for (int i = 0; i < lsize; i++)
      cur = and_table[cur][match_eq_table[left[i]][right[i]]];
   args[0].integer = cur;
}

static void std_match_neq_vec_predef(jit_func_t *func, jit_anchor_t *anchor,
                                     jit_scalar_t *args, tlab_t *tlab)
{
   std_match_eq_vec_predef(func, anchor, args, tlab);
   args[0].integer = not_table[args[0].integer];
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

   // Fast 64-bit hardware division when both operands fit in 64 bits
   if (likely(num_size <= 64 && denom_size <= 64)) {
      const uint64_t dv = __pack_to_u64(denom, denom_size);
      if (unlikely(dv == 0)) {
         __ieee_msg(func, anchor, SEVERITY_ERROR,
                    "NUMERIC_STD.DIVMOD: DIV, MOD, or REM by zero");
         memset(xquot, _0, num_size);
         if (denom_size > num_size) {
            memset(xremain, _0, denom_size - num_size);
            memcpy(xremain + denom_size - num_size, num, num_size);
         }
         else
            memcpy(xremain, num + num_size - denom_size, denom_size);
         return;
      }
      const uint64_t nv = __pack_to_u64(num, num_size);
      __unpack_from_u64(nv / dv, xquot, num_size);
      __unpack_from_u64(nv % dv, xremain, denom_size);
      return;
   }

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

// Bitwise vector-vector, vector-scalar, unary NOT, and unary reductions
// (STD_LOGIC_1164 & NUMERIC_STD)

#define DEFINE_BITWISE_SWAR(OPNAME, OPSTR, TBL, BIN_EXPR)               \
static void ieee_##OPNAME##_vector_impl(jit_func_t *func,               \
                                        jit_anchor_t *anchor,           \
                                        jit_scalar_t *args,             \
                                        tlab_t *tlab, bool is_downto)   \
{                                                                       \
   const int lsize = ffi_array_length(args[3].integer);                 \
   const int rsize = ffi_array_length(args[6].integer);                 \
   const uint8_t *left = args[1].pointer;                               \
   const uint8_t *right = args[4].pointer;                              \
   if (unlikely(lsize != rsize)) {                                      \
      __ieee_msg(func, anchor, SEVERITY_FAILURE,                        \
                 "STD_LOGIC_1164.\"" OPSTR "\": arguments of "          \
                 "overloaded '" OPSTR "' operator are not of the "      \
                 "same length");                                        \
      return;                                                           \
   }                                                                    \
   uint8_t *result = __tlab_alloc(tlab, lsize, 8);                      \
   int pos = 0;                                                         \
   for (; pos + 8 <= lsize; pos += 8) {                                 \
      const uint64_t lw = unaligned_load(left + pos, uint64_t);         \
      const uint64_t rw = unaligned_load(right + pos, uint64_t);        \
      if (likely(IS_01(lw) && IS_01(rw))) {                             \
         const uint64_t res = (BIN_EXPR);                               \
         unaligned_store(result + pos, res, uint64_t);                  \
      }                                                                 \
      else {                                                            \
         for (int k = 0; k < 8; k++)                                    \
            result[pos + k] = TBL[left[pos + k]][right[pos + k]];       \
      }                                                                 \
   }                                                                    \
   for (; pos < lsize; pos++)                                           \
      result[pos] = TBL[left[pos]][right[pos]];                         \
   args[0].pointer = result;                                            \
   args[1].integer = is_downto ? lsize - 1 : 1;                         \
   args[2].integer = is_downto ? ~lsize : lsize;                        \
}                                                                       \
static void ieee_##OPNAME##_vector(jit_func_t *func,                    \
                                   jit_anchor_t *anchor,                \
                                   jit_scalar_t *args, tlab_t *tlab)    \
{                                                                       \
   ieee_##OPNAME##_vector_impl(func, anchor, args, tlab, false);        \
}                                                                       \
static void num_##OPNAME##_vector(jit_func_t *func,                     \
                                  jit_anchor_t *anchor,                 \
                                  jit_scalar_t *args, tlab_t *tlab)     \
{                                                                       \
   ieee_##OPNAME##_vector_impl(func, anchor, args, tlab, true);         \
}                                                                       \
static void ieee_##OPNAME##_vs(jit_func_t *func, jit_anchor_t *anchor,  \
                               jit_scalar_t *args, tlab_t *tlab)        \
{                                                                       \
   const int size = ffi_array_length(args[3].integer);                  \
   const uint8_t *left = args[1].pointer;                               \
   const uint8_t *row = TBL[(uint8_t)args[4].integer];                  \
   uint8_t *result = __tlab_alloc(tlab, size, 8);                       \
   for (int pos = 0; pos < size; pos++)                                 \
      result[pos] = row[left[pos]];                                     \
   args[0].pointer = result;                                            \
   args[1].integer = 1;                                                 \
   args[2].integer = size;                                              \
}                                                                       \
static void ieee_##OPNAME##_sv(jit_func_t *func, jit_anchor_t *anchor,  \
                               jit_scalar_t *args, tlab_t *tlab)        \
{                                                                       \
   __commute_vector_scalar(args);                                       \
   ieee_##OPNAME##_vs(func, anchor, args, tlab);                        \
}

DEFINE_BITWISE_SWAR(and,  "and",  and_table,  lw & rw)
DEFINE_BITWISE_SWAR(or,   "or",   or_table,   lw | rw)
DEFINE_BITWISE_SWAR(xor,  "xor",  xor_table,  (lw ^ rw) | UINT64_C(0x0202020202020202))
DEFINE_BITWISE_SWAR(nand, "nand", nand_table, (lw & rw) ^ UINT64_C(0x0101010101010101))
DEFINE_BITWISE_SWAR(nor,  "nor",  nor_table,  (lw | rw) ^ UINT64_C(0x0101010101010101))
DEFINE_BITWISE_SWAR(xnor, "xnor", xnor_table, ((lw ^ rw) | UINT64_C(0x0202020202020202)) ^ UINT64_C(0x0101010101010101))

static void ieee_not_vector_impl(jit_func_t *func, jit_anchor_t *anchor,
                                 jit_scalar_t *args, tlab_t *tlab,
                                 bool is_downto)
{
   const int size = ffi_array_length(args[3].integer);
   const uint8_t *left = args[1].pointer;
   uint8_t *result = __tlab_alloc(tlab, size, 8);
   int pos = 0;
   for (; pos + 8 <= size; pos += 8) {
      const uint64_t lw = unaligned_load(left + pos, uint64_t);
      if (likely(IS_01(lw)))
         unaligned_store(result + pos, lw ^ UINT64_C(0x0101010101010101), uint64_t);
      else {
         for (int k = 0; k < 8; k++)
            result[pos + k] = not_table[left[pos + k]];
      }
   }
   for (; pos < size; pos++)
      result[pos] = not_table[left[pos]];
   args[0].pointer = result;
   args[1].integer = is_downto ? size - 1 : 1;
   args[2].integer = is_downto ? ~size : size;
}

static void ieee_not_vector(jit_func_t *func, jit_anchor_t *anchor,
                            jit_scalar_t *args, tlab_t *tlab)
{
   ieee_not_vector_impl(func, anchor, args, tlab, false);
}

static void num_not_vector(jit_func_t *func, jit_anchor_t *anchor,
                           jit_scalar_t *args, tlab_t *tlab)
{
   ieee_not_vector_impl(func, anchor, args, tlab, true);
}

#ifdef HAVE_SSE41
#define DEFINE_BITWISE_SSE41(OPNAME, OPSTR, SMALLTBL, ROWTBL)           \
__attribute__((target("sse4.1")))                                       \
static void ieee_##OPNAME##_vector_sse41_impl(jit_func_t *func,         \
                                              jit_anchor_t *anchor,     \
                                              jit_scalar_t *args,       \
                                              tlab_t *tlab,             \
                                              bool is_downto)           \
{                                                                       \
   const int lsize = ffi_array_length(args[3].integer);                 \
   const int rsize = ffi_array_length(args[6].integer);                 \
   const uint8_t *left = args[1].pointer;                               \
   const uint8_t *right = args[4].pointer;                              \
   if (unlikely(lsize != rsize)) {                                      \
      __ieee_msg(func, anchor, SEVERITY_FAILURE,                        \
                 "STD_LOGIC_1164.\"" OPSTR "\": arguments of "          \
                 "overloaded '" OPSTR "' operator are not of the "      \
                 "same length");                                        \
      return;                                                           \
   }                                                                    \
   uint8_t *result = __tlab_alloc(tlab, ALIGN_UP(lsize, 16), 16);       \
   const __m128i left_tbl  = _mm_load_si128((const __m128i *)compress_left);  \
   const __m128i right_tbl = _mm_load_si128((const __m128i *)compress_right); \
   const __m128i op_tbl    = _mm_load_si128((const __m128i *)SMALLTBL); \
   for (int pos = 0; pos < lsize; pos += 16) {                          \
      __m128i left1  = _mm_loadu_si128((const __m128i *)(left + pos));  \
      __m128i right1 = _mm_loadu_si128((const __m128i *)(right + pos)); \
      __m128i left2  = _mm_shuffle_epi8(left_tbl, left1);               \
      __m128i right2 = _mm_shuffle_epi8(right_tbl, right1);             \
      __m128i comb   = _mm_or_si128(left2, right2);                     \
      __m128i out    = _mm_shuffle_epi8(op_tbl, comb);                  \
      _mm_storeu_si128((__m128i *)(result + pos), out);                 \
   }                                                                    \
   args[0].pointer = result;                                            \
   args[1].integer = is_downto ? lsize - 1 : 1;                         \
   args[2].integer = is_downto ? ~lsize : lsize;                        \
}                                                                       \
__attribute__((target("sse4.1")))                                       \
static void ieee_##OPNAME##_vector_sse41(jit_func_t *func,              \
                                         jit_anchor_t *anchor,          \
                                         jit_scalar_t *args,            \
                                         tlab_t *tlab)                  \
{                                                                       \
   ieee_##OPNAME##_vector_sse41_impl(func, anchor, args, tlab, false);  \
}                                                                       \
__attribute__((target("sse4.1")))                                       \
static void num_##OPNAME##_vector_sse41(jit_func_t *func,               \
                                        jit_anchor_t *anchor,           \
                                        jit_scalar_t *args,             \
                                        tlab_t *tlab)                   \
{                                                                       \
   ieee_##OPNAME##_vector_sse41_impl(func, anchor, args, tlab, true);   \
}                                                                       \
__attribute__((target("sse4.1")))                                       \
static void ieee_##OPNAME##_vs_sse41(jit_func_t *func,                  \
                                     jit_anchor_t *anchor,              \
                                     jit_scalar_t *args, tlab_t *tlab)  \
{                                                                       \
   const int size = ffi_array_length(args[3].integer);                  \
   const uint8_t *left = args[1].pointer;                               \
   uint8_t *result = __tlab_alloc(tlab, ALIGN_UP(size, 16), 16);        \
   const __m128i tbl =                                                  \
      _mm_load_si128((const __m128i *)ROWTBL[(uint8_t)args[4].integer]);\
   for (int pos = 0; pos < size; pos += 16) {                           \
      __m128i in = _mm_loadu_si128((const __m128i *)(left + pos));      \
      _mm_storeu_si128((__m128i *)(result + pos),                       \
                       _mm_shuffle_epi8(tbl, in));                      \
   }                                                                    \
   args[0].pointer = result;                                            \
   args[1].integer = 1;                                                 \
   args[2].integer = size;                                              \
}                                                                       \
__attribute__((target("sse4.1")))                                       \
static void ieee_##OPNAME##_sv_sse41(jit_func_t *func,                  \
                                     jit_anchor_t *anchor,              \
                                     jit_scalar_t *args, tlab_t *tlab)  \
{                                                                       \
   __commute_vector_scalar(args);                                       \
   ieee_##OPNAME##_vs_sse41(func, anchor, args, tlab);                  \
}

DEFINE_BITWISE_SSE41(and,  "and",  small_and_table,  and_table)
DEFINE_BITWISE_SSE41(or,   "or",   small_or_table,   or_table)
DEFINE_BITWISE_SSE41(xor,  "xor",  small_xor_table,  xor_table)
DEFINE_BITWISE_SSE41(nand, "nand", small_nand_table, nand_table)
DEFINE_BITWISE_SSE41(nor,  "nor",  small_nor_table,  nor_table)
DEFINE_BITWISE_SSE41(xnor, "xnor", small_xnor_table, xnor_table)

__attribute__((target("sse4.1")))
static void ieee_not_vector_sse41_impl(jit_func_t *func, jit_anchor_t *anchor,
                                       jit_scalar_t *args, tlab_t *tlab,
                                       bool is_downto)
{
   const int lsize = ffi_array_length(args[3].integer);
   const uint8_t *left = args[1].pointer;
   uint8_t *result = __tlab_alloc(tlab, ALIGN_UP(lsize, 16), 16);
   const __m128i not_tbl = _mm_load_si128((const __m128i *)not_table);
   for (int pos = 0; pos < lsize; pos += 16) {
      __m128i input  = _mm_loadu_si128((const __m128i *)(left + pos));
      __m128i output = _mm_shuffle_epi8(not_tbl, input);
      _mm_storeu_si128((__m128i *)(result + pos), output);
   }
   args[0].pointer = result;
   args[1].integer = is_downto ? lsize - 1 : 1;
   args[2].integer = is_downto ? ~lsize : lsize;
}

__attribute__((target("sse4.1")))
static void ieee_not_vector_sse41(jit_func_t *func, jit_anchor_t *anchor,
                                  jit_scalar_t *args, tlab_t *tlab)
{
   ieee_not_vector_sse41_impl(func, anchor, args, tlab, false);
}

__attribute__((target("sse4.1")))
static void num_not_vector_sse41(jit_func_t *func, jit_anchor_t *anchor,
                                 jit_scalar_t *args, tlab_t *tlab)
{
   ieee_not_vector_sse41_impl(func, anchor, args, tlab, true);
}
#endif

#ifdef HAVE_AVX2
#define DEFINE_BITWISE_AVX2(OPNAME, OPSTR, SMALLTBL, ROWTBL)            \
__attribute__((target("avx2")))                                         \
static void ieee_##OPNAME##_vector_avx2_impl(jit_func_t *func,          \
                                             jit_anchor_t *anchor,      \
                                             jit_scalar_t *args,        \
                                             tlab_t *tlab,              \
                                             bool is_downto)            \
{                                                                       \
   const int lsize = ffi_array_length(args[3].integer);                 \
   const int rsize = ffi_array_length(args[6].integer);                 \
   const uint8_t *left = args[1].pointer;                               \
   const uint8_t *right = args[4].pointer;                              \
   if (unlikely(lsize != rsize)) {                                      \
      __ieee_msg(func, anchor, SEVERITY_FAILURE,                        \
                 "STD_LOGIC_1164.\"" OPSTR "\": arguments of "          \
                 "overloaded '" OPSTR "' operator are not of the "      \
                 "same length");                                        \
      return;                                                           \
   }                                                                    \
   uint8_t *result = __tlab_alloc(tlab, ALIGN_UP(lsize, 32), 16);       \
   const __m256i left_tbl  = _mm256_broadcastsi128_si256(               \
      _mm_load_si128((const __m128i *)compress_left));                  \
   const __m256i right_tbl = _mm256_broadcastsi128_si256(               \
      _mm_load_si128((const __m128i *)compress_right));                 \
   const __m256i op_tbl    = _mm256_broadcastsi128_si256(               \
      _mm_load_si128((const __m128i *)SMALLTBL));                       \
   for (int pos = 0; pos < lsize; pos += 32) {                          \
      __m256i left1  = _mm256_loadu_si256((const __m256i *)(left + pos)); \
      __m256i right1 = _mm256_loadu_si256((const __m256i *)(right + pos));\
      __m256i left2  = _mm256_shuffle_epi8(left_tbl, left1);            \
      __m256i right2 = _mm256_shuffle_epi8(right_tbl, right1);          \
      __m256i comb   = _mm256_or_si256(left2, right2);                  \
      __m256i out    = _mm256_shuffle_epi8(op_tbl, comb);               \
      _mm256_storeu_si256((__m256i *)(result + pos), out);              \
   }                                                                    \
   args[0].pointer = result;                                            \
   args[1].integer = is_downto ? lsize - 1 : 1;                         \
   args[2].integer = is_downto ? ~lsize : lsize;                        \
}                                                                       \
__attribute__((target("avx2")))                                         \
static void ieee_##OPNAME##_vector_avx2(jit_func_t *func,               \
                                        jit_anchor_t *anchor,           \
                                        jit_scalar_t *args,             \
                                        tlab_t *tlab)                   \
{                                                                       \
   ieee_##OPNAME##_vector_avx2_impl(func, anchor, args, tlab, false);   \
}                                                                       \
__attribute__((target("avx2")))                                         \
static void num_##OPNAME##_vector_avx2(jit_func_t *func,                \
                                       jit_anchor_t *anchor,            \
                                       jit_scalar_t *args,              \
                                       tlab_t *tlab)                    \
{                                                                       \
   ieee_##OPNAME##_vector_avx2_impl(func, anchor, args, tlab, true);    \
}                                                                       \
__attribute__((target("avx2")))                                         \
static void ieee_##OPNAME##_vs_avx2(jit_func_t *func,                   \
                                    jit_anchor_t *anchor,               \
                                    jit_scalar_t *args, tlab_t *tlab)   \
{                                                                       \
   const int size = ffi_array_length(args[3].integer);                  \
   const uint8_t *left = args[1].pointer;                               \
   uint8_t *result = __tlab_alloc(tlab, ALIGN_UP(size, 32), 16);        \
   const __m256i tbl = _mm256_broadcastsi128_si256(                     \
      _mm_load_si128((const __m128i *)ROWTBL[(uint8_t)args[4].integer]));\
   for (int pos = 0; pos < size; pos += 32) {                           \
      __m256i in = _mm256_loadu_si256((const __m256i *)(left + pos));   \
      _mm256_storeu_si256((__m256i *)(result + pos),                    \
                          _mm256_shuffle_epi8(tbl, in));                \
   }                                                                    \
   args[0].pointer = result;                                            \
   args[1].integer = 1;                                                 \
   args[2].integer = size;                                              \
}                                                                       \
__attribute__((target("avx2")))                                         \
static void ieee_##OPNAME##_sv_avx2(jit_func_t *func,                   \
                                    jit_anchor_t *anchor,               \
                                    jit_scalar_t *args, tlab_t *tlab)   \
{                                                                       \
   __commute_vector_scalar(args);                                       \
   ieee_##OPNAME##_vs_avx2(func, anchor, args, tlab);                   \
}

DEFINE_BITWISE_AVX2(and,  "and",  small_and_table,  and_table)
DEFINE_BITWISE_AVX2(or,   "or",   small_or_table,   or_table)
DEFINE_BITWISE_AVX2(xor,  "xor",  small_xor_table,  xor_table)
DEFINE_BITWISE_AVX2(nand, "nand", small_nand_table, nand_table)
DEFINE_BITWISE_AVX2(nor,  "nor",  small_nor_table,  nor_table)
DEFINE_BITWISE_AVX2(xnor, "xnor", small_xnor_table, xnor_table)

__attribute__((target("avx2")))
static void ieee_not_vector_avx2_impl(jit_func_t *func, jit_anchor_t *anchor,
                                      jit_scalar_t *args, tlab_t *tlab,
                                      bool is_downto)
{
   const int lsize = ffi_array_length(args[3].integer);
   const uint8_t *left = args[1].pointer;
   uint8_t *result = __tlab_alloc(tlab, ALIGN_UP(lsize, 32), 16);
   const __m256i not_tbl = _mm256_broadcastsi128_si256(
      _mm_load_si128((const __m128i *)not_table));
   for (int pos = 0; pos < lsize; pos += 32) {
      __m256i input  = _mm256_loadu_si256((const __m256i *)(left + pos));
      __m256i output = _mm256_shuffle_epi8(not_tbl, input);
      _mm256_storeu_si256((__m256i *)(result + pos), output);
   }
   args[0].pointer = result;
   args[1].integer = is_downto ? lsize - 1 : 1;
   args[2].integer = is_downto ? ~lsize : lsize;
}

__attribute__((target("avx2")))
static void ieee_not_vector_avx2(jit_func_t *func, jit_anchor_t *anchor,
                                 jit_scalar_t *args, tlab_t *tlab)
{
   ieee_not_vector_avx2_impl(func, anchor, args, tlab, false);
}

__attribute__((target("avx2")))
static void num_not_vector_avx2(jit_func_t *func, jit_anchor_t *anchor,
                                jit_scalar_t *args, tlab_t *tlab)
{
   ieee_not_vector_avx2_impl(func, anchor, args, tlab, true);
}
#endif

#ifdef HAVE_NEON
#define DEFINE_BITWISE_NEON(OPNAME, OPSTR, SMALLTBL)                    \
static void ieee_##OPNAME##_vector_neon(jit_func_t *func,               \
                                        jit_anchor_t *anchor,           \
                                        jit_scalar_t *args,             \
                                        tlab_t *tlab)                   \
{                                                                       \
   const int lsize = ffi_array_length(args[3].integer);                 \
   const int rsize = ffi_array_length(args[6].integer);                 \
   const uint8_t *left = args[1].pointer;                               \
   const uint8_t *right = args[4].pointer;                              \
   if (unlikely(lsize != rsize)) {                                      \
      __ieee_msg(func, anchor, SEVERITY_FAILURE,                        \
                 "STD_LOGIC_1164.\"" OPSTR "\": arguments of "          \
                 "overloaded '" OPSTR "' operator are not of the "      \
                 "same length");                                        \
      return;                                                           \
   }                                                                    \
   uint8_t *result = __tlab_alloc(tlab, ALIGN_UP(lsize, 16), 16);       \
   uint8x16_t left_tbl  = vld1q_u8(compress_left);                      \
   uint8x16_t right_tbl = vld1q_u8(compress_right);                     \
   uint8x16_t op_tbl    = vld1q_u8((const uint8_t *)SMALLTBL);          \
   for (int pos = 0; pos < lsize; pos += 16) {                          \
      uint8x16_t left1  = vld1q_u8(left + pos);                         \
      uint8x16_t right1 = vld1q_u8(right + pos);                        \
      uint8x16_t left2  = vqtbl1q_u8(left_tbl, left1);                  \
      uint8x16_t right2 = vqtbl1q_u8(right_tbl, right1);                \
      uint8x16_t comb   = vorrq_u8(left2, right2);                      \
      uint8x16_t out    = vqtbl1q_u8(op_tbl, comb);                     \
      vst1q_u8(result + pos, out);                                      \
   }                                                                    \
   args[0].pointer = result;                                            \
   args[1].integer = 1;                                                 \
   args[2].integer = lsize;                                             \
}

DEFINE_BITWISE_NEON(and, "and", small_and_table)
DEFINE_BITWISE_NEON(or,  "or",  small_or_table)
DEFINE_BITWISE_NEON(xor, "xor", small_xor_table)
#endif

// Unary reductions (exact 9-state with SWAR/AVX2 binary fast paths)
__attribute__((cold, noinline))
static uint8_t __reduce_or_slow(const uint8_t *input, int size, bool inv)
{
   uint8_t prio = 0;
   for (int i = 0; i < size; i++) {
      const uint8_t p = or_reduce_prio[input[i]];
      if (p > prio) {
         prio = p;
         if (prio == 3)
            break;
      }
   }
   return inv ? nor_reduce_unmap[prio] : or_reduce_unmap[prio];
}

__attribute__((cold, noinline))
static uint8_t __reduce_and_slow(const uint8_t *input, int size, bool inv)
{
   uint8_t prio = 0;
   for (int i = 0; i < size; i++) {
      const uint8_t p = and_reduce_prio[input[i]];
      if (p > prio) {
         prio = p;
         if (prio == 3)
            break;
      }
   }
   return inv ? nand_reduce_unmap[prio] : and_reduce_unmap[prio];
}

__attribute__((cold, noinline))
static uint8_t __reduce_xor_slow(const uint8_t *input, int size, bool inv)
{
   uint8_t res = _0;
   for (int i = size - 1; i >= 0; i--)
      res = xor_table[input[i]][res];
   return inv ? not_table[res] : res;
}

static uint8_t __reduce_or_swar(const uint8_t *input, int size, bool inv)
{
   if (unlikely(size == 0))
      return inv ? _1 : _0;

   uint64_t acc_or = 0;
   int pos = 0;
   for (; pos + 8 <= size; pos += 8) {
      const uint64_t w = unaligned_load(input + pos, uint64_t);
      if (unlikely(!IS_01(w)))
         return __reduce_or_slow(input, size, inv);
      acc_or |= w;
   }
   bool any1 = (acc_or & UINT64_C(0x0101010101010101)) != 0;
   for (; pos < size; pos++) {
      const uint8_t b = input[pos];
      if (unlikely(!IS_01(b)))
         return __reduce_or_slow(input, size, inv);
      any1 |= (b & 1);
   }
   return (any1 ^ inv) ? _1 : _0;
}

static uint8_t __reduce_and_swar(const uint8_t *input, int size, bool inv)
{
   if (unlikely(size == 0))
      return inv ? _0 : _1;

   uint64_t acc_and = ~UINT64_C(0);
   int pos = 0;
   for (; pos + 8 <= size; pos += 8) {
      const uint64_t w = unaligned_load(input + pos, uint64_t);
      if (unlikely(!IS_01(w)))
         return __reduce_and_slow(input, size, inv);
      acc_and &= w;
   }
   bool all1 = (acc_and & UINT64_C(0x0101010101010101))
               == UINT64_C(0x0101010101010101);
   for (; pos < size; pos++) {
      const uint8_t b = input[pos];
      if (unlikely(!IS_01(b)))
         return __reduce_and_slow(input, size, inv);
      all1 &= (b & 1);
   }
   return (all1 ^ inv) ? _1 : _0;
}

static uint8_t __reduce_xor_swar(const uint8_t *input, int size, bool inv)
{
   if (unlikely(size == 0))
      return inv ? _1 : _0;

   uint64_t acc_xor = 0;
   int pos = 0;
   for (; pos + 8 <= size; pos += 8) {
      const uint64_t w = unaligned_load(input + pos, uint64_t);
      if (unlikely(!IS_01(w)))
         return __reduce_xor_slow(input, size, inv);
      acc_xor ^= w;
   }
   int parity = __builtin_popcountll(acc_xor & UINT64_C(0x0101010101010101)) & 1;
   for (; pos < size; pos++) {
      const uint8_t b = input[pos];
      if (unlikely(!IS_01(b)))
         return __reduce_xor_slow(input, size, inv);
      parity ^= (b & 1);
   }
   return (parity ^ inv) ? _1 : _0;
}

static void std_reduce_or(jit_func_t *func, jit_anchor_t *anchor,
                          jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = __reduce_or_swar(args[1].pointer,
                                      ffi_array_length(args[3].integer), false);
}

static void std_reduce_nor(jit_func_t *func, jit_anchor_t *anchor,
                           jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = __reduce_or_swar(args[1].pointer,
                                      ffi_array_length(args[3].integer), true);
}

static void std_reduce_and(jit_func_t *func, jit_anchor_t *anchor,
                           jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = __reduce_and_swar(args[1].pointer,
                                       ffi_array_length(args[3].integer), false);
}

static void std_reduce_nand(jit_func_t *func, jit_anchor_t *anchor,
                            jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = __reduce_and_swar(args[1].pointer,
                                       ffi_array_length(args[3].integer), true);
}

static void std_reduce_xor(jit_func_t *func, jit_anchor_t *anchor,
                           jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = __reduce_xor_swar(args[1].pointer,
                                       ffi_array_length(args[3].integer), false);
}

static void std_reduce_xnor(jit_func_t *func, jit_anchor_t *anchor,
                            jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = __reduce_xor_swar(args[1].pointer,
                                       ffi_array_length(args[3].integer), true);
}

#ifdef HAVE_AVX2
__attribute__((target("avx2"), always_inline))
static inline uint8_t __reduce_or_avx2(const uint8_t *input, int size, bool inv)
{
   if (unlikely(size == 0))
      return inv ? _1 : _0;

   const __m256i m_fe = _mm256_set1_epi8((char)0xfe);
   const __m256i m_02 = _mm256_set1_epi8(0x02);
   const __m256i m_01 = _mm256_set1_epi8(0x01);

   __m256i acc_or = _mm256_setzero_si256();
   __m256i acc_bad = _mm256_setzero_si256();
   int pos = 0;
   for (; pos + 32 <= size; pos += 32) {
      __m256i v = _mm256_loadu_si256((const __m256i *)(input + pos));
      acc_bad = _mm256_or_si256(acc_bad,
                                _mm256_xor_si256(_mm256_and_si256(v, m_fe), m_02));
      acc_or = _mm256_or_si256(acc_or, v);
   }

   bool tail_any1 = false;
   if (pos < size) {
      const int rem = size - pos;
      __m256i v = _mm256_loadu_si256((const __m256i *)(input + pos));
      __m256i eq = _mm256_cmpeq_epi8(_mm256_and_si256(v, m_fe), m_02);
      const uint32_t ok = (uint32_t)_mm256_movemask_epi8(eq);
      const uint32_t mask = ~0U >> (32 - rem);
      if (unlikely((~ok & mask) != 0))
         return __reduce_or_slow(input, size, inv);
      const uint32_t ones = (uint32_t)_mm256_movemask_epi8(_mm256_slli_epi16(v, 7));
      tail_any1 = (ones & mask) != 0;
   }

   if (unlikely(!_mm256_testz_si256(acc_bad, acc_bad)))
      return __reduce_or_slow(input, size, inv);

   const bool any1 = tail_any1 || !_mm256_testz_si256(acc_or, m_01);
   return (any1 ^ inv) ? _1 : _0;
}

__attribute__((target("avx2"), always_inline))
static inline uint8_t __reduce_and_avx2(const uint8_t *input, int size, bool inv)
{
   if (unlikely(size == 0))
      return inv ? _0 : _1;

   const __m256i m_fe = _mm256_set1_epi8((char)0xfe);
   const __m256i m_02 = _mm256_set1_epi8(0x02);
   const __m256i m_03 = _mm256_set1_epi8(0x03);

   __m256i acc_and = m_03;
   __m256i acc_bad = _mm256_setzero_si256();
   int pos = 0;
   for (; pos + 32 <= size; pos += 32) {
      __m256i v = _mm256_loadu_si256((const __m256i *)(input + pos));
      acc_bad = _mm256_or_si256(acc_bad,
                                _mm256_xor_si256(_mm256_and_si256(v, m_fe), m_02));
      acc_and = _mm256_and_si256(acc_and, v);
   }

   bool tail_all1 = true;
   if (pos < size) {
      const int rem = size - pos;
      __m256i v = _mm256_loadu_si256((const __m256i *)(input + pos));
      __m256i eq = _mm256_cmpeq_epi8(_mm256_and_si256(v, m_fe), m_02);
      const uint32_t ok = (uint32_t)_mm256_movemask_epi8(eq);
      const uint32_t mask = ~0U >> (32 - rem);
      if (unlikely((~ok & mask) != 0))
         return __reduce_and_slow(input, size, inv);
      const uint32_t ones = (uint32_t)_mm256_movemask_epi8(_mm256_slli_epi16(v, 7));
      tail_all1 = (ones & mask) == mask;
   }

   if (unlikely(!_mm256_testz_si256(acc_bad, acc_bad)))
      return __reduce_and_slow(input, size, inv);

   const bool all1 = tail_all1
      && (_mm256_movemask_epi8(_mm256_cmpeq_epi8(acc_and, m_03)) == -1);
   return (all1 ^ inv) ? _1 : _0;
}

__attribute__((target("avx2"), always_inline))
static inline uint8_t __reduce_xor_avx2(const uint8_t *input, int size, bool inv)
{
   if (unlikely(size == 0))
      return inv ? _1 : _0;

   const __m256i m_fe = _mm256_set1_epi8((char)0xfe);
   const __m256i m_02 = _mm256_set1_epi8(0x02);

   __m256i acc_xor = _mm256_setzero_si256();
   __m256i acc_bad = _mm256_setzero_si256();
   int pos = 0;
   for (; pos + 32 <= size; pos += 32) {
      __m256i v = _mm256_loadu_si256((const __m256i *)(input + pos));
      acc_bad = _mm256_or_si256(acc_bad,
                                _mm256_xor_si256(_mm256_and_si256(v, m_fe), m_02));
      acc_xor = _mm256_xor_si256(acc_xor, v);
   }

   uint32_t tail_m = 0;
   if (pos < size) {
      const int rem = size - pos;
      __m256i v = _mm256_loadu_si256((const __m256i *)(input + pos));
      __m256i eq = _mm256_cmpeq_epi8(_mm256_and_si256(v, m_fe), m_02);
      const uint32_t ok = (uint32_t)_mm256_movemask_epi8(eq);
      const uint32_t mask = ~0U >> (32 - rem);
      if (unlikely((~ok & mask) != 0))
         return __reduce_xor_slow(input, size, inv);
      tail_m = (uint32_t)_mm256_movemask_epi8(_mm256_slli_epi16(v, 7)) & mask;
   }

   if (unlikely(!_mm256_testz_si256(acc_bad, acc_bad)))
      return __reduce_xor_slow(input, size, inv);

   const uint32_t m = (uint32_t)_mm256_movemask_epi8(_mm256_slli_epi16(acc_xor, 7));
   const int parity = (__builtin_popcount(m) ^ __builtin_popcount(tail_m)) & 1;
   return (parity ^ inv) ? _1 : _0;
}

__attribute__((target("avx2")))
static void std_reduce_or_avx2(jit_func_t *func, jit_anchor_t *anchor,
                               jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = __reduce_or_avx2(args[1].pointer,
                                      ffi_array_length(args[3].integer), false);
}

__attribute__((target("avx2")))
static void std_reduce_nor_avx2(jit_func_t *func, jit_anchor_t *anchor,
                                jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = __reduce_or_avx2(args[1].pointer,
                                      ffi_array_length(args[3].integer), true);
}

__attribute__((target("avx2")))
static void std_reduce_and_avx2(jit_func_t *func, jit_anchor_t *anchor,
                                jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = __reduce_and_avx2(args[1].pointer,
                                       ffi_array_length(args[3].integer), false);
}

__attribute__((target("avx2")))
static void std_reduce_nand_avx2(jit_func_t *func, jit_anchor_t *anchor,
                                 jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = __reduce_and_avx2(args[1].pointer,
                                       ffi_array_length(args[3].integer), true);
}

__attribute__((target("avx2")))
static void std_reduce_xor_avx2(jit_func_t *func, jit_anchor_t *anchor,
                                jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = __reduce_xor_avx2(args[1].pointer,
                                       ffi_array_length(args[3].integer), false);
}

__attribute__((target("avx2")))
static void std_reduce_xnor_avx2(jit_func_t *func, jit_anchor_t *anchor,
                                 jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = __reduce_xor_avx2(args[1].pointer,
                                       ffi_array_length(args[3].integer), true);
}
#endif

__attribute__((always_inline))
static inline bool __is_x(uint8_t arg)
{
   return !IS_01LH(arg);
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
      memset(result, *(const uint8_t *)input, pad);

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
   if (unlikely(length == 0))
      fail_in_interpreter(func, anchor, args, tlab);

   if (likely(lsize >= 1 && rsize >= 1 && length <= 64)) {
      uint64_t lv, rv;
      if (likely(__try_pack_u64_swar(left, lsize, &lv)
                 && __try_pack_u64_swar(right, rsize, &rv))) {
         uint8_t *result = __tlab_alloc(tlab, length, 8);
         __unpack_from_u64(lv + rv, result, length);
         args[0].pointer = result;
         args[1].integer = length - 1;
         args[2].integer = ~length;
         return;
      }
   }

   uint8_t *result = __tlab_alloc(tlab, length, 8);
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
   if (unlikely(length == 0))
      fail_in_interpreter(func, anchor, args, tlab);

   if (likely(lsize >= 1 && rsize >= 1 && length <= 64)) {
      uint64_t lv, rv;
      if (likely(__try_pack_u64_swar(left, lsize, &lv)
                 && __try_pack_u64_swar(right, rsize, &rv))) {
         const uint64_t ls = (uint64_t)__sign_extend_u64(lv, lsize);
         const uint64_t rs = (uint64_t)__sign_extend_u64(rv, rsize);
         uint8_t *result = __tlab_alloc(tlab, length, 8);
         __unpack_from_u64(ls + rs, result, length);
         args[0].pointer = result;
         args[1].integer = length - 1;
         args[2].integer = ~length;
         return;
      }
   }

   uint8_t *result = __tlab_alloc(tlab, length, 8);
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
   if (unlikely(length == 0))
      fail_in_interpreter(func, anchor, args, tlab);

   if (likely(lsize >= 1 && rsize >= 1 && length <= 64)) {
      uint64_t lv, rv;
      if (likely(__try_pack_u64_swar(left, lsize, &lv)
                 && __try_pack_u64_swar(right, rsize, &rv))) {
         uint8_t *result = __tlab_alloc(tlab, length, 8);
         __unpack_from_u64(lv - rv, result, length);
         args[0].pointer = result;
         args[1].integer = length - 1;
         args[2].integer = ~length;
         return;
      }
   }

   uint8_t *result = __tlab_alloc(tlab, length, 8);
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

static void synopsys_minus_signed(jit_func_t *func, jit_anchor_t *anchor,
                                  jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[3].integer);
   const int rsize = ffi_array_length(args[6].integer);
   const uint8_t *left = args[1].pointer;
   const uint8_t *right = args[4].pointer;

   const int length = MAX(lsize, rsize);
   if (unlikely(length == 0))
      fail_in_interpreter(func, anchor, args, tlab);

   if (likely(lsize >= 1 && rsize >= 1 && length <= 64)) {
      uint64_t lv, rv;
      if (likely(__try_pack_u64_swar(left, lsize, &lv)
                 && __try_pack_u64_swar(right, rsize, &rv))) {
         const uint64_t ls = (uint64_t)__sign_extend_u64(lv, lsize);
         const uint64_t rs = (uint64_t)__sign_extend_u64(rv, rsize);
         uint8_t *result = __tlab_alloc(tlab, length, 8);
         __unpack_from_u64(ls - rs, result, length);
         args[0].pointer = result;
         args[1].integer = length - 1;
         args[2].integer = ~length;
         return;
      }
   }

   uint8_t *result = __tlab_alloc(tlab, length, 8);
   left = __conv_signed(func, anchor, tlab, left, lsize, length);
   right = __conv_signed(func, anchor, tlab, right, rsize, length);

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

   const int length = lsize + rsize;
   if (likely(lsize <= 64 && rsize <= 64)) {
      uint64_t lv, rv;
      if (likely(__try_pack_u64_swar(left, lsize, &lv)
                 && __try_pack_u64_swar(right, rsize, &rv))) {
         uint8_t *pa = __tlab_alloc(tlab, length, 8);
         if (length <= 64)
            __unpack_from_u64(lv * rv, pa, length);
         else {
            unsigned __int128 prod =
               (unsigned __int128)lv * (unsigned __int128)rv;
            const int hi = length - 64;
            __unpack_from_u64((uint64_t)(prod >> 64), pa, hi);
            __unpack_from_u64((uint64_t)prod, pa + hi, 64);
         }
         args[0].pointer = pa;
         args[1].integer = length - 1;
         args[2].integer = ~length;
         return;
      }
   }

   left = __conv_unsigned(func, anchor, tlab, left, lsize, lsize);
   right = __conv_unsigned(func, anchor, tlab, right, rsize, rsize);

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

   const int length = lsize + rsize;
   if (likely(lsize <= 64 && rsize <= 64)) {
      uint64_t lv, rv;
      if (likely(__try_pack_u64_swar(left, lsize, &lv)
                 && __try_pack_u64_swar(right, rsize, &rv))) {
         const int64_t ls = __sign_extend_u64(lv, lsize);
         const int64_t rs = __sign_extend_u64(rv, rsize);
         uint8_t *pa = __tlab_alloc(tlab, length, 8);
         if (length <= 64)
            __unpack_from_u64((uint64_t)(ls * rs), pa, length);
         else {
            unsigned __int128 prod =
               (unsigned __int128)((__int128)ls * (__int128)rs);
            const int hi = length - 64;
            __unpack_from_u64((uint64_t)(prod >> 64), pa, hi);
            __unpack_from_u64((uint64_t)prod, pa + hi, 64);
         }
         args[0].pointer = pa;
         args[1].integer = length - 1;
         args[2].integer = ~length;
         return;
      }
   }

   left = __conv_signed(func, anchor, tlab, left, lsize, lsize);
   right = __conv_signed(func, anchor, tlab, right, rsize, rsize);

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

   if (likely(lsize >= 1 && rsize >= 1 && lsize <= 64 && rsize <= 64)) {
      uint64_t lv, rv;
      if (likely(__try_pack_u64_swar(left, lsize, &lv)
                 && __try_pack_u64_swar(right, rsize, &rv))) {
         args[0].integer = (lv == rv);
         return;
      }
   }

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

   if (likely(lsize >= 1 && rsize >= 1 && lsize <= 64 && rsize <= 64)) {
      uint64_t lv, rv;
      if (likely(__try_pack_u64_swar(left, lsize, &lv)
                 && __try_pack_u64_swar(right, rsize, &rv))) {
         args[0].integer = (__sign_extend_u64(lv, lsize)
                            == __sign_extend_u64(rv, rsize));
         return;
      }
   }

   const uint32_t mark = __tlab_mark(tlab);

   left = __conv_signed(func, anchor, tlab, left, lsize, length);
   right = __conv_signed(func, anchor, tlab, right, rsize, length);

   args[0].integer = (memcmp(left, right, length) == 0);

   __tlab_restore(tlab, mark);
}

#ifdef HAVE_AVX2
__attribute__((target("avx2")))
static void byte_vector_equal_avx2(jit_func_t *func, jit_anchor_t *anchor,
                                   jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[2].integer);
   const int rsize = ffi_array_length(args[5].integer);
   const uint8_t *left = args[0].pointer;
   const uint8_t *right = args[3].pointer;

   args[0].integer = 0;

   if (lsize != rsize)
      return;

   if (lsize == 0) {
      args[0].integer = 1;
      return;
   }

   int pos = 0;
   for (; pos + 32 <= lsize; pos += 32) {
      __m256i l = _mm256_loadu_si256((const __m256i *)(left + pos));
      __m256i r = _mm256_loadu_si256((const __m256i *)(right + pos));
      if (_mm256_movemask_epi8(_mm256_cmpeq_epi8(l, r)) != -1)
         return;
   }

   if (pos < lsize) {
      const int rem = lsize - pos;
      __m256i l = _mm256_loadu_si256((const __m256i *)(left + pos));
      __m256i r = _mm256_loadu_si256((const __m256i *)(right + pos));
      const uint32_t eq = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(l, r));
      const uint32_t mask = ~0U >> (32 - rem);
      if ((~eq & mask) != 0)
         return;
   }

   args[0].integer = 1;
}
#endif

#ifdef HAVE_SSE41
__attribute__((target("sse4.1")))
static void byte_vector_equal_sse41(jit_func_t *func, jit_anchor_t *anchor,
                                    jit_scalar_t *args, tlab_t *tlab)
{
   const int lsize = ffi_array_length(args[2].integer);
   const int rsize = ffi_array_length(args[5].integer);
   const uint8_t *left = args[0].pointer;
   const uint8_t *right = args[3].pointer;

   args[0].integer = 0;

   if (lsize != rsize)
      return;

   int pos = 0;
   for (; pos + 16 <= lsize; pos += 16) {
      __m128i left1  = _mm_loadu_si128((const __m128i *)(left + pos));
      __m128i right1 = _mm_loadu_si128((const __m128i *)(right + pos));
      if (_mm_movemask_epi8(_mm_cmpeq_epi8(left1, right1)) != 0xffff)
         return;
   }

   if (pos < lsize) {
      const int rem = lsize - pos;
      __m128i left1  = _mm_loadu_si128((const __m128i *)(left + pos));
      __m128i right1 = _mm_loadu_si128((const __m128i *)(right + pos));
      const uint32_t eq = (uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(left1, right1));
      const uint32_t mask = (1U << rem) - 1;
      if ((~eq & mask) != 0)
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

   if (nchars == 0) {
      args[0].pointer = NULL;
      return;
   }

   assert(*line != NULL);

   const int64_t raw_len = (*line)->dims[0].length;
   const int length = ffi_array_length(raw_len);
   assert(nchars <= length);

   (*line)->ptr += nchars;
   const int64_t new_len = length - nchars;
   if (new_len == 0) {
      (*line)->dims[0].left = 1;
      (*line)->dims[0].length = 0;
   }
   else if (ffi_array_dir(raw_len)) {
      (*line)->dims[0].left -= nchars;
      (*line)->dims[0].length = ~new_len;
   }
   else {
      (*line)->dims[0].left += nchars;
      (*line)->dims[0].length = new_len;
   }

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

static void sv_poll_watch_sig(jit_func_t *func, jit_anchor_t *caller,
                              jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_poll_watch_sig(args[0].pointer, args[1].integer, args[2].integer);
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

static void sv_island_begin(jit_func_t *func, jit_anchor_t *caller,
                            jit_scalar_t *args, tlab_t *tlab)
{
   args[0].pointer = x_sv_island_begin(args[0].pointer,
                                       (uint32_t)args[1].integer,
                                       (uint32_t)args[2].integer,
                                       (uint32_t)args[3].integer);
}

static void sv_island_set_sparse(jit_func_t *func, jit_anchor_t *caller,
                                 jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_island_set_sparse(args[0].pointer,
                          (uint32_t)args[1].integer,
                          (uint32_t)args[2].integer);
   args[0].integer = 0;
}

static void sv_island_sparse_write(jit_func_t *func, jit_anchor_t *caller,
                                   jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_island_sparse_write((void *)(uintptr_t)args[0].integer,
                            (uint32_t)args[1].integer,
                            (uint32_t)args[2].integer,
                            (uint32_t)args[3].integer,
                            args[4].pointer);
   args[0].integer = 0;
}

static void sv_island_bind_sig(jit_func_t *func, jit_anchor_t *caller,
                               jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_island_bind_sig(args[0].pointer, (uint32_t)args[1].integer,
                        args[2].pointer, (int32_t)args[3].integer,
                        (uint32_t)args[4].integer, (uint32_t)args[5].integer,
                        (uint32_t)args[6].integer, (uint32_t)args[7].integer,
                        (uint32_t)args[8].integer);
   args[0].integer = 0;
}

static void sv_island_bind_gen_sig(jit_func_t *func, jit_anchor_t *caller,
                                   jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_island_bind_gen_sig(args[0].pointer, (uint32_t)args[1].integer,
                            (void *)(uintptr_t)args[2].integer,
                            (void *)(uintptr_t)args[3].integer,
                            (uint32_t)args[4].integer,
                            (uint32_t)args[5].integer,
                            (uint32_t)args[6].integer,
                            (uint32_t)args[7].integer,
                            (uint32_t)args[8].integer);
   args[0].integer = 0;
}

static void sv_island_set_gen_key(jit_func_t *func, jit_anchor_t *caller,
                                  jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_island_set_gen_key(args[0].pointer, (uint32_t)args[1].integer,
                           (uint64_t)args[2].integer);
   args[0].integer = 0;
}

static void sv_island_attach_sig(jit_func_t *func, jit_anchor_t *caller,
                                 jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_island_attach_sig(args[0].pointer, (uint32_t)args[1].integer,
                          args[2].pointer, (int32_t)args[3].integer);
   args[0].integer = 0;
}

static void sv_island_apply_forces(jit_func_t *func, jit_anchor_t *caller,
                                   jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_island_apply_forces((void *)(uintptr_t)args[0].integer);
   args[0].integer = 0;
}

static void sv_island_add_domain(jit_func_t *func, jit_anchor_t *caller,
                                 jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_island_add_domain(args[0].pointer, (uint32_t)args[1].integer,
                          args[2].pointer, (uint32_t)args[3].integer,
                          (int32_t)args[4].integer, (uint32_t)args[5].integer,
                          (uint32_t)args[6].integer);
   args[0].integer = 0;
}

static void sv_island_add_domain_write(jit_func_t *func, jit_anchor_t *caller,
                                       jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_island_add_domain_write(args[0].pointer, (uint32_t)args[1].integer,
                                (uint32_t)args[2].integer);
   args[0].integer = 0;
}

static void sv_island_finish(jit_func_t *func, jit_anchor_t *caller,
                             jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_island_finish(args[0].pointer);
   args[0].integer = 0;
}

static void sv_island_set_variant_a(jit_func_t *func, jit_anchor_t *caller,
                                    jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_island_set_variant_a(args[0].pointer, args[1].pointer,
                             args[2].pointer,
                             (uint32_t)args[3].integer,
                             (uint32_t)args[4].integer,
                             (uint32_t)args[5].integer,
                             (uint32_t)args[6].integer);
   args[0].integer = 0;
}

static void sv_island_bind_sig_a(jit_func_t *func, jit_anchor_t *caller,
                                 jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_island_bind_sig_a(args[0].pointer, (uint32_t)args[1].integer,
                          (uint32_t)args[2].integer, (uint32_t)args[3].integer,
                          (uint32_t)args[4].integer, (uint32_t)args[5].integer);
   args[0].integer = 0;
}

static void sv_island_add_domain_a(jit_func_t *func, jit_anchor_t *caller,
                                   jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_island_add_domain_a(args[0].pointer, (uint32_t)args[1].integer,
                            args[2].pointer, args[3].pointer);
   args[0].integer = 0;
}

static void sv_island_mark_ext_read(jit_func_t *func, jit_anchor_t *caller,
                                    jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_island_mark_ext_read(args[0].pointer);
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

static void sv_sva_defer_tick(jit_func_t *func, jit_anchor_t *caller,
                              jit_scalar_t *args, tlab_t *tlab)
{
   sva_t *s = args[0].pointer;
   const uint64_t leaves = args[1].integer;
   const int disable = args[2].integer;
   if (x_sv_sva_defer_tick(s, leaves, disable))
      args[0].integer = 1;
   else {
      sva_set_caller(sva_call_thunk, func->jit);
      (void)sva_tick(s, leaves, disable);
      args[0].integer = 0;
   }
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
   uint64_t draw;
   const uint32_t r = model_rng_draw(&draw)
      ? (uint32_t)draw : rtl_dist_uniform(&seed, INT32_MIN, INT32_MAX);
   args[0].integer = range == 0 ? 0 : r % range;
}

static void sv_rng_get(jit_func_t *func, jit_anchor_t *caller,
                       jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = x_sv_rng_get();
}

static void sv_rng_pick(jit_func_t *func, jit_anchor_t *caller,
                        jit_scalar_t *args, tlab_t *tlab)
{
   // The stream of an object if it has one, otherwise that of the process

   if (args[0].integer == 0)
      args[0].integer = x_sv_rng_get();
}

static void sv_rng_back(jit_func_t *func, jit_anchor_t *caller,
                        jit_scalar_t *args, tlab_t *tlab)
{
   // Returns the new state of the object: the stream of the process
   // advanced instead if the object has none

   if (args[0].integer == 0) {
      x_sv_rng_set(args[1].integer);
      args[0].integer = 0;
   }
   else
      args[0].integer = args[1].integer;
}

static void sv_rng_new(jit_func_t *func, jit_anchor_t *caller,
                       jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = x_sv_rng_new();
}

static void sv_rng_seed(jit_func_t *func, jit_anchor_t *caller,
                        jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_rng_seed(args[0].pointer, args[1].integer);
   args[0].integer = 0;
}

static void sv_rng_state(jit_func_t *func, jit_anchor_t *caller,
                         jit_scalar_t *args, tlab_t *tlab)
{
   args[0].integer = x_sv_rng_state(args[0].pointer);
}

static void sv_rng_restore(jit_func_t *func, jit_anchor_t *caller,
                           jit_scalar_t *args, tlab_t *tlab)
{
   x_sv_rng_restore(args[0].pointer, args[1].integer);
   args[0].integer = 0;
}

static void sv_rng_ensure(jit_func_t *func, jit_anchor_t *caller,
                          jit_scalar_t *args, tlab_t *tlab)
{
   // An object that has no stream yet takes one from its process, or a
   // sequence which is the same on every run

   if (args[0].integer == 0) {
      int64_t s = x_sv_rng_new();
      args[0].integer = s != 0 ? s : x_sv_rng_fresh();
   }
}

static void sv_rng_format(jit_func_t *func, jit_anchor_t *caller,
                          jit_scalar_t *args, tlab_t *tlab)
{
   // The text of a random state: a prefix and 16 hex digits

   uint8_t *dest = args[1].pointer;
   char buf[32];
   snprintf(buf, sizeof(buf), "nvc:%016" PRIx64, (uint64_t)args[0].integer);
   memcpy(dest, buf, 20);
   args[0].integer = 0;
}

static void sv_rng_parse(jit_func_t *func, jit_anchor_t *caller,
                         jit_scalar_t *args, tlab_t *tlab)
{
   // Zero if the text is not a state this simulator produced

   const char *str = args[0].pointer;
   const int64_t len = ffi_array_length(args[2].integer);

   uint64_t value = 0;
   bool ok = len == 20 && memcmp(str, "nvc:", 4) == 0;
   for (int i = 4; ok && i < 20; i++) {
      const int c = str[i];
      if (c >= '0' && c <= '9')
         value = (value << 4) | (c - '0');
      else if (c >= 'a' && c <= 'f')
         value = (value << 4) | (c - 'a' + 10);
      else
         ok = false;
   }

   args[0].integer = ok ? (int64_t)value : 0;
}

static void sv_str_cmp(jit_func_t *func, jit_anchor_t *caller,
                       jit_scalar_t *args, tlab_t *tlab)
{
   const uint8_t *left = args[0].pointer, *right = args[3].pointer;
   const int64_t llen = ffi_array_length(args[2].integer);
   const int64_t rlen = ffi_array_length(args[5].integer);
   const bool icase = args[6].integer;
   const int64_t n = MIN(llen, rlen);

   int64_t result = 0;
   if (!icase) {
      if (left != right) {
         int64_t i = 0;
         for (; i + 8 <= n; i += 8) {
            uint64_t lw, rw;
            memcpy(&lw, left + i, 8);
            memcpy(&rw, right + i, 8);
            if (lw != rw) {
               const uint64_t diff = lw ^ rw;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
               const int b = __builtin_ctzll(diff) >> 3;
#else
               const int b = __builtin_clzll(diff) >> 3;
#endif
               result = (int)left[i + b] - (int)right[i + b];
               i = n;
               break;
            }
         }
         for (; i < n; i++) {
            if (left[i] != right[i]) {
               result = (int)left[i] - (int)right[i];
               break;
            }
         }
      }
   }
   else {
      for (int64_t i = 0; i < n && result == 0; i++) {
         const int lc = tolower(left[i]);
         const int rc = tolower(right[i]);
         result = lc - rc;
      }
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

   crv_set_caller(caller);
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

static void sv_packed_edge(jit_func_t *func, jit_anchor_t *caller,
                           jit_scalar_t *args, tlab_t *tlab)
{
   sig_shared_t *ss = args[0].pointer;
   const uint32_t offset = args[1].integer;
   const int64_t edge = args[2].integer;

   const rt_signal_t *s = container_of(ss, rt_signal_t, shared);
   const uint32_t width = s->bit_width;
   const uint32_t bit = width - 1 - offset;
   const int nwords = (width + 63) / 64;
   const int w = bit / 64, sh = bit % 64;

   const uint64_t *cur_a = (const uint64_t *)ss->data;
   const uint64_t *last_a = (const uint64_t *)(ss->data + ss->size);

   uint8_t cur_cmp, last_cmp;
   if (ss->flags & SIG_F_PACKED4) {
      const uint64_t *cur_b = cur_a + nwords;
      const uint64_t *last_b = last_a + nwords;
      const uint8_t ca = (cur_a[w] >> sh) & 1;
      const uint8_t cb = (cur_b[w] >> sh) & 1;
      const uint8_t la = (last_a[w] >> sh) & 1;
      const uint8_t lb = (last_b[w] >> sh) & 1;
      cur_cmp = (cb << 1) | ca;
      last_cmp = (lb << 1) | la;
   }
   else {
      cur_cmp = (cur_a[w] >> sh) & 1;
      last_cmp = (last_a[w] >> sh) & 1;
   }

   static const uint8_t table_entries[16] = {
      0, 1, 1, 1,
      2, 0, 2, 2,
      2, 1, 0, 0,
      2, 1, 0, 0,
   };

   args[0].integer = table_entries[last_cmp * 4 + cur_cmp] == edge;
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
   { "__sv_poll_watch_sig", sv_poll_watch_sig },
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
   { "__sv_rng_get", sv_rng_get },
   { "__sv_rng_pick", sv_rng_pick },
   { "__sv_rng_back", sv_rng_back },
   { "__sv_rng_new", sv_rng_new },
   { "__sv_rng_seed", sv_rng_seed },
   { "__sv_rng_state", sv_rng_state },
   { "__sv_rng_restore", sv_rng_restore },
   { "__sv_rng_ensure", sv_rng_ensure },
   { "__sv_rng_format", sv_rng_format },
   { "__sv_rng_parse", sv_rng_parse },
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
   { "__sv_sva_defer_tick", sv_sva_defer_tick },
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
   { "__sv_packed_edge", sv_packed_edge },
   { "__sv_cstr_len", sv_cstr_len },
   { "__sv_fread", sv_fread },
   { "__sv_fread_count", sv_fread_count },
#define NUMERIC_ISA_ENTRIES(SFX, FEAT)                                  \
   { NS "\"+\"(" U U ")" U, ieee_plus_unsigned##SFX, FEAT },            \
   { NS "\"+\"(" UU UU ")" UU, ieee_plus_unsigned##SFX, FEAT },         \
   { NS "\"+\"(" U "N)" U, ieee_plus_unsigned_natural##SFX, FEAT },     \
   { NS "\"+\"(" UU "N)" UU, ieee_plus_unsigned_natural##SFX, FEAT },   \
   { NS "\"+\"(N" U ")" U, ieee_plus_natural_unsigned##SFX, FEAT },     \
   { NS "\"+\"(N" UU ")" UU, ieee_plus_natural_unsigned##SFX, FEAT },   \
   { NS "\"+\"(" UU "U)" UU, ieee_plus_vec_logic##SFX, FEAT },          \
   { NS "\"+\"(U" UU ")" UU, ieee_plus_logic_vec##SFX, FEAT },          \
   { NS "\"+\"(" S S ")" S, ieee_plus_signed##SFX, FEAT },              \
   { NS "\"+\"(" US US ")" US, ieee_plus_signed##SFX, FEAT },           \
   { NS "\"+\"(" S "I)" S, ieee_plus_signed_integer##SFX, FEAT },       \
   { NS "\"+\"(" US "I)" US, ieee_plus_signed_integer##SFX, FEAT },     \
   { NS "\"+\"(I" S ")" S, ieee_plus_integer_signed##SFX, FEAT },       \
   { NS "\"+\"(I" US ")" US, ieee_plus_integer_signed##SFX, FEAT },     \
   { NS "\"+\"(" US "U)" US, ieee_plus_vec_logic##SFX, FEAT },          \
   { NS "\"+\"(U" US ")" US, ieee_plus_logic_vec##SFX, FEAT },          \
   { NS "\"-\"(" U U ")" U, ieee_minus_unsigned##SFX, FEAT },           \
   { NS "\"-\"(" UU UU ")" UU, ieee_minus_unsigned##SFX, FEAT },        \
   { NS "\"-\"(" U "N)" U, ieee_minus_unsigned_natural##SFX, FEAT },    \
   { NS "\"-\"(" UU "N)" UU, ieee_minus_unsigned_natural##SFX, FEAT },  \
   { NS "\"-\"(N" U ")" U, ieee_minus_natural_unsigned##SFX, FEAT },    \
   { NS "\"-\"(N" UU ")" UU, ieee_minus_natural_unsigned##SFX, FEAT },  \
   { NS "\"-\"(" UU "U)" UU, ieee_minus_vec_logic##SFX, FEAT },         \
   { NS "\"-\"(U" UU ")" UU, ieee_minus_logic_vec##SFX, FEAT },         \
   { NS "\"-\"(" S S ")" S, ieee_minus_signed##SFX, FEAT },             \
   { NS "\"-\"(" US US ")" US, ieee_minus_signed##SFX, FEAT },          \
   { NS "\"-\"(" S "I)" S, ieee_minus_signed_integer##SFX, FEAT },      \
   { NS "\"-\"(" US "I)" US, ieee_minus_signed_integer##SFX, FEAT },    \
   { NS "\"-\"(I" S ")" S, ieee_minus_integer_signed##SFX, FEAT },      \
   { NS "\"-\"(I" US ")" US, ieee_minus_integer_signed##SFX, FEAT },    \
   { NS "\"-\"(" US "U)" US, ieee_minus_vec_logic##SFX, FEAT },         \
   { NS "\"-\"(U" US ")" US, ieee_minus_logic_vec##SFX, FEAT },         \
   { NS "\"-\"(" S ")" S, ieee_neg_signed##SFX, FEAT },                 \
   { NS "\"-\"(" US ")" US, ieee_neg_signed##SFX, FEAT },               \
   { NS "\"abs\"(" S ")" S, ieee_abs_signed##SFX, FEAT },               \
   { NS "\"abs\"(" US ")" US, ieee_abs_signed##SFX, FEAT },             \
   { NS "\"*\"(" U U ")" U, ieee_mul_unsigned##SFX, FEAT },             \
   { NS "\"*\"(" UU UU ")" UU, ieee_mul_unsigned##SFX, FEAT },          \
   { NS "\"*\"(" S S ")" S, ieee_mul_signed##SFX, FEAT },               \
   { NS "\"*\"(" US US ")" US, ieee_mul_signed##SFX, FEAT },            \
   { NS "\"=\"(" U U ")B", ieee_eq_unsigned##SFX, FEAT },               \
   { NS "\"=\"(" UU UU ")B", ieee_eq_unsigned##SFX, FEAT },             \
   { NS "\"=\"(" U "N)B", ieee_eq_unat##SFX, FEAT },                    \
   { NS "\"=\"(" UU "N)B", ieee_eq_unat##SFX, FEAT },                   \
   { NS "\"=\"(N" U ")B", ieee_eq_natu##SFX, FEAT },                    \
   { NS "\"=\"(N" UU ")B", ieee_eq_natu##SFX, FEAT },                   \
   { NS "\"=\"(" S S ")B", ieee_eq_signed##SFX, FEAT },                 \
   { NS "\"=\"(" US US ")B", ieee_eq_signed##SFX, FEAT },               \
   { NS "\"=\"(" S "I)B", ieee_eq_sint##SFX, FEAT },                    \
   { NS "\"=\"(" US "I)B", ieee_eq_sint##SFX, FEAT },                   \
   { NS "\"=\"(I" S ")B", ieee_eq_ints##SFX, FEAT },                    \
   { NS "\"=\"(I" US ")B", ieee_eq_ints##SFX, FEAT },                   \
   { NS "\"/=\"(" U U ")B", ieee_neq_unsigned##SFX, FEAT },             \
   { NS "\"/=\"(" UU UU ")B", ieee_neq_unsigned##SFX, FEAT },           \
   { NS "\"/=\"(" U "N)B", ieee_neq_unat##SFX, FEAT },                  \
   { NS "\"/=\"(" UU "N)B", ieee_neq_unat##SFX, FEAT },                 \
   { NS "\"/=\"(N" U ")B", ieee_neq_natu##SFX, FEAT },                  \
   { NS "\"/=\"(N" UU ")B", ieee_neq_natu##SFX, FEAT },                 \
   { NS "\"/=\"(" S S ")B", ieee_neq_signed##SFX, FEAT },               \
   { NS "\"/=\"(" US US ")B", ieee_neq_signed##SFX, FEAT },             \
   { NS "\"/=\"(" S "I)B", ieee_neq_sint##SFX, FEAT },                  \
   { NS "\"/=\"(" US "I)B", ieee_neq_sint##SFX, FEAT },                 \
   { NS "\"/=\"(I" S ")B", ieee_neq_ints##SFX, FEAT },                  \
   { NS "\"/=\"(I" US ")B", ieee_neq_ints##SFX, FEAT },                 \
   { NS "\"<\"(" U U ")B", ieee_less_unsigned##SFX, FEAT },             \
   { NS "\"<\"(" UU UU ")B", ieee_less_unsigned##SFX, FEAT },           \
   { NS "\"<\"(" U "N)B", ieee_less_unat##SFX, FEAT },                  \
   { NS "\"<\"(" UU "N)B", ieee_less_unat##SFX, FEAT },                 \
   { NS "\"<\"(N" U ")B", ieee_less_natu##SFX, FEAT },                  \
   { NS "\"<\"(N" UU ")B", ieee_less_natu##SFX, FEAT },                 \
   { NS "\"<\"(" S S ")B", ieee_less_signed##SFX, FEAT },               \
   { NS "\"<\"(" US US ")B", ieee_less_signed##SFX, FEAT },             \
   { NS "\"<\"(" S "I)B", ieee_less_sint##SFX, FEAT },                  \
   { NS "\"<\"(" US "I)B", ieee_less_sint##SFX, FEAT },                 \
   { NS "\"<\"(I" S ")B", ieee_less_ints##SFX, FEAT },                  \
   { NS "\"<\"(I" US ")B", ieee_less_ints##SFX, FEAT },                 \
   { NS "\"<=\"(" U U ")B", ieee_leq_unsigned##SFX, FEAT },             \
   { NS "\"<=\"(" UU UU ")B", ieee_leq_unsigned##SFX, FEAT },           \
   { NS "\"<=\"(" U "N)B", ieee_leq_unat##SFX, FEAT },                  \
   { NS "\"<=\"(" UU "N)B", ieee_leq_unat##SFX, FEAT },                 \
   { NS "\"<=\"(N" U ")B", ieee_leq_natu##SFX, FEAT },                  \
   { NS "\"<=\"(N" UU ")B", ieee_leq_natu##SFX, FEAT },                 \
   { NS "\"<=\"(" S S ")B", ieee_leq_signed##SFX, FEAT },               \
   { NS "\"<=\"(" US US ")B", ieee_leq_signed##SFX, FEAT },             \
   { NS "\"<=\"(" S "I)B", ieee_leq_sint##SFX, FEAT },                  \
   { NS "\"<=\"(" US "I)B", ieee_leq_sint##SFX, FEAT },                 \
   { NS "\"<=\"(I" S ")B", ieee_leq_ints##SFX, FEAT },                  \
   { NS "\"<=\"(I" US ")B", ieee_leq_ints##SFX, FEAT },                 \
   { NS "\">\"(" U U ")B", ieee_greater_unsigned##SFX, FEAT },          \
   { NS "\">\"(" UU UU ")B", ieee_greater_unsigned##SFX, FEAT },        \
   { NS "\">\"(" U "N)B", ieee_greater_unat##SFX, FEAT },               \
   { NS "\">\"(" UU "N)B", ieee_greater_unat##SFX, FEAT },              \
   { NS "\">\"(N" U ")B", ieee_greater_natu##SFX, FEAT },               \
   { NS "\">\"(N" UU ")B", ieee_greater_natu##SFX, FEAT },              \
   { NS "\">\"(" S S ")B", ieee_greater_signed##SFX, FEAT },            \
   { NS "\">\"(" US US ")B", ieee_greater_signed##SFX, FEAT },          \
   { NS "\">\"(" S "I)B", ieee_greater_sint##SFX, FEAT },               \
   { NS "\">\"(" US "I)B", ieee_greater_sint##SFX, FEAT },              \
   { NS "\">\"(I" S ")B", ieee_greater_ints##SFX, FEAT },               \
   { NS "\">\"(I" US ")B", ieee_greater_ints##SFX, FEAT },              \
   { NS "\">=\"(" U U ")B", ieee_geq_unsigned##SFX, FEAT },             \
   { NS "\">=\"(" UU UU ")B", ieee_geq_unsigned##SFX, FEAT },           \
   { NS "\">=\"(" U "N)B", ieee_geq_unat##SFX, FEAT },                  \
   { NS "\">=\"(" UU "N)B", ieee_geq_unat##SFX, FEAT },                 \
   { NS "\">=\"(N" U ")B", ieee_geq_natu##SFX, FEAT },                  \
   { NS "\">=\"(N" UU ")B", ieee_geq_natu##SFX, FEAT },                 \
   { NS "\">=\"(" S S ")B", ieee_geq_signed##SFX, FEAT },               \
   { NS "\">=\"(" US US ")B", ieee_geq_signed##SFX, FEAT },             \
   { NS "\">=\"(" S "I)B", ieee_geq_sint##SFX, FEAT },                  \
   { NS "\">=\"(" US "I)B", ieee_geq_sint##SFX, FEAT },                 \
   { NS "\">=\"(I" S ")B", ieee_geq_ints##SFX, FEAT },                  \
   { NS "\">=\"(I" US ")B", ieee_geq_ints##SFX, FEAT },                 \
   { NS "TO_INTEGER(" U ")N", ieee_to_integer_unsigned##SFX, FEAT },    \
   { NS "TO_INTEGER(" UU ")N", ieee_to_integer_unsigned##SFX, FEAT },   \
   { NS "TO_INTEGER(" S ")I", ieee_to_integer_signed##SFX, FEAT },      \
   { NS "TO_INTEGER(" US ")I", ieee_to_integer_signed##SFX, FEAT },     \
   { NS "TO_UNSIGNED(NN)" U, ieee_to_unsigned##SFX, FEAT },             \
   { NS "TO_UNSIGNED(NN)" UU, ieee_to_unsigned##SFX, FEAT },            \
   { NS "TO_UNSIGNED(N" UU ")" UU, ieee_to_unsigned_res##SFX, FEAT },   \
   { NS "TO_SIGNED(IN)" S, ieee_to_signed##SFX, FEAT },                 \
   { NS "TO_SIGNED(IN)" US, ieee_to_signed##SFX, FEAT },                \
   { NS "TO_SIGNED(I" US ")" US, ieee_to_signed_res##SFX, FEAT },       \
   { NS "\"?=\"(" UU UU ")U", ieee_match_eq_unsigned##SFX, FEAT },      \
   { NS "\"?=\"(" US US ")U", ieee_match_eq_signed##SFX, FEAT },        \
   { NS "\"?/=\"(" UU UU ")U", ieee_match_neq_unsigned##SFX, FEAT },    \
   { NS "\"?/=\"(" US US ")U", ieee_match_neq_signed##SFX, FEAT }

#define BITWISE_ISA_ENTRIES(SFX, FEAT)                                  \
   { SL "\"and\"(VV)V", ieee_and_vector##SFX, FEAT },                   \
   { SL "\"and\"(YY)Y", ieee_and_vector##SFX, FEAT },                   \
   { SL "\"or\"(VV)V", ieee_or_vector##SFX, FEAT },                     \
   { SL "\"or\"(YY)Y", ieee_or_vector##SFX, FEAT },                     \
   { SL "\"xor\"(VV)V", ieee_xor_vector##SFX, FEAT },                   \
   { SL "\"xor\"(YY)Y", ieee_xor_vector##SFX, FEAT },                   \
   { SL "\"nand\"(VV)V", ieee_nand_vector##SFX, FEAT },                 \
   { SL "\"nand\"(YY)Y", ieee_nand_vector##SFX, FEAT },                 \
   { SL "\"nor\"(VV)V", ieee_nor_vector##SFX, FEAT },                   \
   { SL "\"nor\"(YY)Y", ieee_nor_vector##SFX, FEAT },                   \
   { SL "\"xnor\"(VV)V", ieee_xnor_vector##SFX, FEAT },                 \
   { SL "\"xnor\"(YY)Y", ieee_xnor_vector##SFX, FEAT },                 \
   { SL "\"not\"(V)V", ieee_not_vector##SFX, FEAT },                    \
   { SL "\"not\"(Y)Y", ieee_not_vector##SFX, FEAT },                    \
   { SL "\"and\"(YU)Y", ieee_and_vs##SFX, FEAT },                       \
   { SL "\"and\"(UY)Y", ieee_and_sv##SFX, FEAT },                       \
   { SL "\"or\"(YU)Y", ieee_or_vs##SFX, FEAT },                         \
   { SL "\"or\"(UY)Y", ieee_or_sv##SFX, FEAT },                         \
   { SL "\"xor\"(YU)Y", ieee_xor_vs##SFX, FEAT },                       \
   { SL "\"xor\"(UY)Y", ieee_xor_sv##SFX, FEAT },                       \
   { SL "\"nand\"(YU)Y", ieee_nand_vs##SFX, FEAT },                     \
   { SL "\"nand\"(UY)Y", ieee_nand_sv##SFX, FEAT },                     \
   { SL "\"nor\"(YU)Y", ieee_nor_vs##SFX, FEAT },                       \
   { SL "\"nor\"(UY)Y", ieee_nor_sv##SFX, FEAT },                       \
   { SL "\"xnor\"(YU)Y", ieee_xnor_vs##SFX, FEAT },                     \
   { SL "\"xnor\"(UY)Y", ieee_xnor_sv##SFX, FEAT },                     \
   { NS "\"and\"(" U U ")" U, num_and_vector##SFX, FEAT },              \
   { NS "\"and\"(" UU UU ")" UU, num_and_vector##SFX, FEAT },           \
   { NS "\"and\"(" S S ")" S, num_and_vector##SFX, FEAT },              \
   { NS "\"and\"(" US US ")" US, num_and_vector##SFX, FEAT },           \
   { NS "\"or\"(" U U ")" U, num_or_vector##SFX, FEAT },                \
   { NS "\"or\"(" UU UU ")" UU, num_or_vector##SFX, FEAT },             \
   { NS "\"or\"(" S S ")" S, num_or_vector##SFX, FEAT },                \
   { NS "\"or\"(" US US ")" US, num_or_vector##SFX, FEAT },             \
   { NS "\"xor\"(" U U ")" U, num_xor_vector##SFX, FEAT },              \
   { NS "\"xor\"(" UU UU ")" UU, num_xor_vector##SFX, FEAT },           \
   { NS "\"xor\"(" S S ")" S, num_xor_vector##SFX, FEAT },              \
   { NS "\"xor\"(" US US ")" US, num_xor_vector##SFX, FEAT },           \
   { NS "\"nand\"(" U U ")" U, num_nand_vector##SFX, FEAT },            \
   { NS "\"nand\"(" UU UU ")" UU, num_nand_vector##SFX, FEAT },         \
   { NS "\"nand\"(" S S ")" S, num_nand_vector##SFX, FEAT },            \
   { NS "\"nand\"(" US US ")" US, num_nand_vector##SFX, FEAT },         \
   { NS "\"nor\"(" U U ")" U, num_nor_vector##SFX, FEAT },              \
   { NS "\"nor\"(" UU UU ")" UU, num_nor_vector##SFX, FEAT },           \
   { NS "\"nor\"(" S S ")" S, num_nor_vector##SFX, FEAT },              \
   { NS "\"nor\"(" US US ")" US, num_nor_vector##SFX, FEAT },           \
   { NS "\"xnor\"(" U U ")" U, num_xnor_vector##SFX, FEAT },            \
   { NS "\"xnor\"(" UU UU ")" UU, num_xnor_vector##SFX, FEAT },         \
   { NS "\"xnor\"(" S S ")" S, num_xnor_vector##SFX, FEAT },            \
   { NS "\"xnor\"(" US US ")" US, num_xnor_vector##SFX, FEAT },         \
   { NS "\"not\"(" U ")" U, num_not_vector##SFX, FEAT },                \
   { NS "\"not\"(" UU ")" UU, num_not_vector##SFX, FEAT },              \
   { NS "\"not\"(" S ")" S, num_not_vector##SFX, FEAT },                \
   { NS "\"not\"(" US ")" US, num_not_vector##SFX, FEAT },              \
   { NS "\"and\"(" UU "U)" UU, ieee_and_vs##SFX, FEAT },                \
   { NS "\"and\"(U" UU ")" UU, ieee_and_sv##SFX, FEAT },                \
   { NS "\"and\"(" US "U)" US, ieee_and_vs##SFX, FEAT },                \
   { NS "\"and\"(U" US ")" US, ieee_and_sv##SFX, FEAT },                \
   { NS "\"or\"(" UU "U)" UU, ieee_or_vs##SFX, FEAT },                  \
   { NS "\"or\"(U" UU ")" UU, ieee_or_sv##SFX, FEAT },                  \
   { NS "\"or\"(" US "U)" US, ieee_or_vs##SFX, FEAT },                  \
   { NS "\"or\"(U" US ")" US, ieee_or_sv##SFX, FEAT },                  \
   { NS "\"xor\"(" UU "U)" UU, ieee_xor_vs##SFX, FEAT },                \
   { NS "\"xor\"(U" UU ")" UU, ieee_xor_sv##SFX, FEAT },                \
   { NS "\"xor\"(" US "U)" US, ieee_xor_vs##SFX, FEAT },                \
   { NS "\"xor\"(U" US ")" US, ieee_xor_sv##SFX, FEAT },                \
   { NS "\"nand\"(" UU "U)" UU, ieee_nand_vs##SFX, FEAT },              \
   { NS "\"nand\"(U" UU ")" UU, ieee_nand_sv##SFX, FEAT },              \
   { NS "\"nand\"(" US "U)" US, ieee_nand_vs##SFX, FEAT },              \
   { NS "\"nand\"(U" US ")" US, ieee_nand_sv##SFX, FEAT },              \
   { NS "\"nor\"(" UU "U)" UU, ieee_nor_vs##SFX, FEAT },                \
   { NS "\"nor\"(U" UU ")" UU, ieee_nor_sv##SFX, FEAT },                \
   { NS "\"nor\"(" US "U)" US, ieee_nor_vs##SFX, FEAT },                \
   { NS "\"nor\"(U" US ")" US, ieee_nor_sv##SFX, FEAT },                \
   { NS "\"xnor\"(" UU "U)" UU, ieee_xnor_vs##SFX, FEAT },              \
   { NS "\"xnor\"(U" UU ")" UU, ieee_xnor_sv##SFX, FEAT },              \
   { NS "\"xnor\"(" US "U)" US, ieee_xnor_vs##SFX, FEAT },              \
   { NS "\"xnor\"(U" US ")" US, ieee_xnor_sv##SFX, FEAT }

#define CVT_ISA_ENTRIES(SFX, FEAT)                                      \
   { SL "TO_X01(V)V", std_to_x01##SFX, FEAT },                          \
   { SL "TO_X01(Y)Y", std_to_x01##SFX, FEAT },                          \
   { NS "TO_X01(" UU ")" UU, std_to_x01##SFX, FEAT },                   \
   { NS "TO_X01(" US ")" US, std_to_x01##SFX, FEAT },                   \
   { SL "TO_X01Z(V)V", std_to_x01z##SFX, FEAT },                        \
   { SL "TO_X01Z(Y)Y", std_to_x01z##SFX, FEAT },                        \
   { NS "TO_X01Z(" UU ")" UU, std_to_x01z##SFX, FEAT },                 \
   { NS "TO_X01Z(" US ")" US, std_to_x01z##SFX, FEAT },                 \
   { SL "TO_UX01(V)V", std_to_ux01##SFX, FEAT },                        \
   { SL "TO_UX01(Y)Y", std_to_ux01##SFX, FEAT },                        \
   { NS "TO_UX01(" UU ")" UU, std_to_ux01##SFX, FEAT },                 \
   { NS "TO_UX01(" US ")" US, std_to_ux01##SFX, FEAT },                 \
   { SL "IS_X(V)B", std_is_x_vec##SFX, FEAT },                          \
   { SL "IS_X(Y)B", std_is_x_vec##SFX, FEAT },                          \
   { NS "IS_X(" UU ")B", std_is_x_vec##SFX, FEAT },                     \
   { NS "IS_X(" US ")B", std_is_x_vec##SFX, FEAT }

#define REDUCE_ISA_ENTRIES(SFX, FEAT)                                   \
   { SL "\"or\"(Y)U", std_reduce_or##SFX, FEAT },                       \
   { SL "\"nor\"(Y)U", std_reduce_nor##SFX, FEAT },                     \
   { SL "\"and\"(Y)U", std_reduce_and##SFX, FEAT },                     \
   { SL "\"nand\"(Y)U", std_reduce_nand##SFX, FEAT },                   \
   { SL "\"xor\"(Y)U", std_reduce_xor##SFX, FEAT },                     \
   { SL "\"xnor\"(Y)U", std_reduce_xnor##SFX, FEAT },                   \
   { NS "\"or\"(" UU ")U", std_reduce_or##SFX, FEAT },                  \
   { NS "\"or\"(" US ")U", std_reduce_or##SFX, FEAT },                  \
   { NS "\"nor\"(" UU ")U", std_reduce_nor##SFX, FEAT },                \
   { NS "\"nor\"(" US ")U", std_reduce_nor##SFX, FEAT },                \
   { NS "\"and\"(" UU ")U", std_reduce_and##SFX, FEAT },                \
   { NS "\"and\"(" US ")U", std_reduce_and##SFX, FEAT },                \
   { NS "\"nand\"(" UU ")U", std_reduce_nand##SFX, FEAT },              \
   { NS "\"nand\"(" US ")U", std_reduce_nand##SFX, FEAT },              \
   { NS "\"xor\"(" UU ")U", std_reduce_xor##SFX, FEAT },                \
   { NS "\"xor\"(" US ")U", std_reduce_xor##SFX, FEAT },                \
   { NS "\"xnor\"(" UU ")U", std_reduce_xnor##SFX, FEAT },              \
   { NS "\"xnor\"(" US ")U", std_reduce_xnor##SFX, FEAT }

#ifdef HAVE_AVX2
   NUMERIC_ISA_ENTRIES(_avx2, CPU_AVX2),
   BITWISE_ISA_ENTRIES(_avx2, CPU_AVX2),
   CVT_ISA_ENTRIES(_avx2, CPU_AVX2),
   REDUCE_ISA_ENTRIES(_avx2, CPU_AVX2),
   { SL "TO_01(YU)Y", std_to_01_avx2, CPU_AVX2 },
   { SL "TO_HSTRING(Y)S", std_to_hstring_avx2, CPU_AVX2 },
   { NS "TO_HSTRING(" UU ")S", std_to_hstring_avx2, CPU_AVX2 },
   { NS "TO_HSTRING(" US ")S", ieee_to_hstring_signed_avx2, CPU_AVX2 },
   { SL "\"=\"(VV)B$predef", byte_vector_equal_avx2, CPU_AVX2 },
   { SL "\"=\"(YY)B$predef", byte_vector_equal_avx2, CPU_AVX2 },
   { ST "\"=\"(QQ)B$predef", byte_vector_equal_avx2, CPU_AVX2 },
   { ST "\"=\"(SS)B$predef", byte_vector_equal_avx2, CPU_AVX2 },
#endif
#ifdef HAVE_SSE41
   NUMERIC_ISA_ENTRIES(_sse41, CPU_SSE41),
   BITWISE_ISA_ENTRIES(_sse41, CPU_SSE41),
   CVT_ISA_ENTRIES(_sse41, CPU_SSE41),
   { SL "\"=\"(VV)B$predef", byte_vector_equal_sse41, CPU_SSE41 },
   { SL "\"=\"(YY)B$predef", byte_vector_equal_sse41, CPU_SSE41 },
   { ST "\"=\"(QQ)B$predef", byte_vector_equal_sse41, CPU_SSE41 },
   { ST "\"=\"(SS)B$predef", byte_vector_equal_sse41, CPU_SSE41 },
#endif
#ifdef HAVE_NEON
   { SL "\"and\"(VV)V", ieee_and_vector_neon, CPU_NEON },
   { SL "\"and\"(YY)Y", ieee_and_vector_neon, CPU_NEON },
   { SL "\"or\"(VV)V", ieee_or_vector_neon, CPU_NEON },
   { SL "\"or\"(YY)Y", ieee_or_vector_neon, CPU_NEON },
   { SL "\"xor\"(VV)V", ieee_xor_vector_neon, CPU_NEON },
   { SL "\"xor\"(YY)Y", ieee_xor_vector_neon, CPU_NEON },
   { SL "\"=\"(VV)B$predef", byte_vector_equal_neon, CPU_NEON },
   { SL "\"=\"(YY)B$predef", byte_vector_equal_neon, CPU_NEON },
   { ST "\"=\"(QQ)B$predef", byte_vector_equal_neon, CPU_NEON },
   { ST "\"=\"(SS)B$predef", byte_vector_equal_neon, CPU_NEON },
#endif
   NUMERIC_ISA_ENTRIES(, 0),
   BITWISE_ISA_ENTRIES(, 0),
   CVT_ISA_ENTRIES(, 0),
   REDUCE_ISA_ENTRIES(, 0),
   { NS "DIVMOD(" UU UU UU UU ")", ieee_divmod },
   { NS "DIVMOD(" U U U U ")", ieee_divmod },
   { SL "IS_X(U)B", std_is_x_scalar },
   { SL "TO_01(YU)Y", std_to_01 },
   { NS "TO_01(" U "L)" U, ieee_to_01_unsigned },
   { NS "TO_01(" UU "U)" UU, ieee_to_01_unsigned },
   { NS "TO_01(" S "L)" S, ieee_to_01_signed },
   { NS "TO_01(" US "U)" US, ieee_to_01_signed },
   { NS "RESIZE(" U "N)" U, ieee_resize_unsigned },
   { NS "RESIZE(" UU "N)" UU, ieee_resize_unsigned },
   { NS "RESIZE(" UU UU ")" UU, ieee_resize_unsigned_res },
   { NS "RESIZE(" S "N)" S, ieee_resize_signed },
   { NS "RESIZE(" US "N)" US, ieee_resize_signed },
   { NS "RESIZE(" US US ")" US, ieee_resize_signed_res },
   { NS "SHIFT_LEFT(" U "N)" U, ieee_shift_left },
   { NS "SHIFT_LEFT(" UU "N)" UU, ieee_shift_left },
   { NS "SHIFT_LEFT(" S "N)" S, ieee_shift_left },
   { NS "SHIFT_LEFT(" US "N)" US, ieee_shift_left },
   { NS "SHIFT_RIGHT(" U "N)" U, ieee_shift_right_unsigned },
   { NS "SHIFT_RIGHT(" UU "N)" UU, ieee_shift_right_unsigned },
   { NS "SHIFT_RIGHT(" S "N)" S, ieee_shift_right_signed },
   { NS "SHIFT_RIGHT(" US "N)" US, ieee_shift_right_signed },
   { NS "ROTATE_LEFT(" U "N)" U, ieee_rotate_left },
   { NS "ROTATE_LEFT(" UU "N)" UU, ieee_rotate_left },
   { NS "ROTATE_LEFT(" S "N)" S, ieee_rotate_left },
   { NS "ROTATE_LEFT(" US "N)" US, ieee_rotate_left },
   { NS "ROTATE_RIGHT(" U "N)" U, ieee_rotate_right },
   { NS "ROTATE_RIGHT(" UU "N)" UU, ieee_rotate_right },
   { NS "ROTATE_RIGHT(" S "N)" S, ieee_rotate_right },
   { NS "ROTATE_RIGHT(" US "N)" US, ieee_rotate_right },
   { NS "\"sll\"(" U "I)" U, ieee_sll },
   { NS "\"sll\"(" UU "I)" UU, ieee_sll },
   { NS "\"sll\"(" S "I)" S, ieee_sll },
   { NS "\"sll\"(" US "I)" US, ieee_sll },
   { NS "\"srl\"(" U "I)" U, ieee_srl },
   { NS "\"srl\"(" UU "I)" UU, ieee_srl },
   { NS "\"srl\"(" S "I)" S, ieee_srl },
   { NS "\"srl\"(" US "I)" US, ieee_srl },
   { NS "\"sla\"(" UU "I)" UU, ieee_sll },
   { NS "\"sla\"(" US "I)" US, ieee_sla_signed },
   { NS "\"sra\"(" UU "I)" UU, ieee_srl },
   { NS "\"sra\"(" US "I)" US, ieee_sra_signed },
   { NS "\"rol\"(" U "I)" U, ieee_rol },
   { NS "\"rol\"(" UU "I)" UU, ieee_rol },
   { NS "\"rol\"(" S "I)" S, ieee_rol },
   { NS "\"rol\"(" US "I)" US, ieee_rol },
   { NS "\"ror\"(" U "I)" U, ieee_ror },
   { NS "\"ror\"(" UU "I)" UU, ieee_ror },
   { NS "\"ror\"(" S "I)" S, ieee_ror },
   { NS "\"ror\"(" US "I)" US, ieee_ror },
   { NS "XSLL(YN)Y", ieee_shift_left },
   { NS "XSRL(YN)Y", ieee_shift_right_unsigned },
   { NS "XSRA(YN)Y", ieee_shift_right_signed },
   { NS "XROL(YN)Y", ieee_rotate_left },
   { NS "XROR(YN)Y", ieee_rotate_right },
   { SL "\"sll\"(YI)Y", std_sll },
   { SL "\"srl\"(YI)Y", std_srl },
   { SL "\"rol\"(YI)Y", std_rol },
   { SL "\"ror\"(YI)Y", std_ror },
   { SL "TO_HSTRING(Y)S", std_to_hstring },
   { NS "TO_HSTRING(" UU ")S", std_to_hstring },
   { NS "TO_HSTRING(" US ")S", ieee_to_hstring_signed },
   { SL "TO_OSTRING(Y)S", std_to_ostring },
   { NS "TO_OSTRING(" UU ")S", std_to_ostring },
   { NS "TO_OSTRING(" US ")S", ieee_to_ostring_signed },
   { NS "STD_MATCH(UU)B", ieee_std_match_scalar },
   { NS "STD_MATCH(" U U ")B", ieee_std_match_vec },
   { NS "STD_MATCH(" UU UU ")B", ieee_std_match_vec },
   { NS "STD_MATCH(" S S ")B", ieee_std_match_vec },
   { NS "STD_MATCH(" US US ")B", ieee_std_match_vec },
   { NS "STD_MATCH(VV)B", ieee_std_match_vec },
   { NS "STD_MATCH(YY)B", ieee_std_match_vec },
   { NS "\"?=\"(" UU "N)U", ieee_match_eq_unat },
   { NS "\"?=\"(N" UU ")U", ieee_match_eq_natu },
   { NS "\"?/=\"(" UU "N)U", ieee_match_neq_unat },
   { NS "\"?/=\"(N" UU ")U", ieee_match_neq_natu },
   { NS "\"?=\"(" US "I)U", ieee_match_eq_sint },
   { NS "\"?=\"(I" US ")U", ieee_match_eq_ints },
   { NS "\"?/=\"(" US "I)U", ieee_match_neq_sint },
   { NS "\"?/=\"(I" US ")U", ieee_match_neq_ints },
   { SL "\"?=\"(UU)U$predef", std_match_eq_scalar_predef },
   { SL "\"?/=\"(UU)U$predef", std_match_neq_scalar_predef },
   { SL "\"?=\"(YY)U$predef", std_match_eq_vec_predef },
   { SL "\"?/=\"(YY)U$predef", std_match_neq_vec_predef },
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
   { SA "\"-\"(" AS AS ")" AS, synopsys_minus_signed },
   { SA "\"-\"(" AS AS ")V", synopsys_minus_signed },
   { SS "\"-\"(VV)V", synopsys_minus_signed },
   { SS "\"-\"(YY)Y", synopsys_minus_signed },
   { SA "\"*\"(" AU AU ")" AU, synopsys_mul_unsigned },
   { SA "\"*\"(" AU AU ")V", synopsys_mul_unsigned },
   { SU "\"*\"(VV)V", synopsys_mul_unsigned },
   { SU "\"*\"(YY)Y", synopsys_mul_unsigned },
   { SA "\"*\"(" AS AS ")" AS, synopsys_mul_signed },
   { SA "\"*\"(" AS AS ")V", synopsys_mul_signed },
   { SS "\"*\"(VV)V", synopsys_mul_signed },
   { SS "\"*\"(YY)Y", synopsys_mul_signed },
   { SA "\"=\"(" AU AU ")B", synopsys_eql_unsigned },
   { SU "\"=\"(VV)B", synopsys_eql_unsigned },
   { SU "\"=\"(YY)B", synopsys_eql_unsigned },
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
   static ident_t island_begin_i = NULL, island_bind_i = NULL,
      island_bind_gen_i = NULL,
      island_gen_key_i = NULL, island_attach_i = NULL, island_apply_f_i = NULL,
      island_add_dom_i = NULL, island_add_dom_w_i = NULL,
      island_finish_i = NULL, island_ext_read_i = NULL,
      island_set_sparse_i = NULL, island_sparse_write_i = NULL,
      island_variant_a_i = NULL, island_bind_a_i = NULL,
      island_add_dom_a_i = NULL;

   INIT_ONCE({
         dispatch_func_i    = ident_new("__nvc_protected_dispatch_func");
         dispatch_proc_i    = ident_new("__nvc_protected_dispatch_proc");
         island_begin_i     = ident_new("__sv_island_begin");
         island_bind_i      = ident_new("__sv_island_bind_sig");
         island_bind_gen_i  = ident_new("__sv_island_bind_gen_sig");
         island_gen_key_i   = ident_new("__sv_island_set_gen_key");
         island_attach_i    = ident_new("__sv_island_attach_sig");
         island_apply_f_i   = ident_new("__sv_island_apply_forces");
         island_add_dom_i   = ident_new("__sv_island_add_domain");
         island_add_dom_w_i = ident_new("__sv_island_add_domain_write");
         island_finish_i    = ident_new("__sv_island_finish");
         island_ext_read_i  = ident_new("__sv_island_mark_ext_read");
         island_set_sparse_i = ident_new("__sv_island_set_sparse");
         island_sparse_write_i = ident_new("__sv_island_sparse_write");
         island_variant_a_i = ident_new("__sv_island_set_variant_a");
         island_bind_a_i    = ident_new("__sv_island_bind_sig_a");
         island_add_dom_a_i = ident_new("__sv_island_add_domain_a");

         const bool want_intrinsics = !!opt_get_int(OPT_JIT_INTRINSICS);

#if ASAN_ENABLED
         const bool want_vector = false;   // Reads past end of input (benign)
#else
         const bool want_vector = !!opt_get_int(OPT_VECTOR_INTRINSICS);
#endif

         ieee_packed_add = __ieee_packed_add_scalar;
         ieee_to_01_slow_fn = __ieee_to_01_slow_swar;

         cpu_feature_t mask = 0;
#ifdef HAVE_SSE41
         if (want_vector && __builtin_cpu_supports("sse4.1")) {
            mask |= CPU_SSE41;
            ieee_packed_add = __ieee_packed_add_sse41;
            ieee_to_01_slow_fn = __ieee_to_01_slow_sse41;
         }
#endif
#ifdef HAVE_AVX2
         if (want_vector && __builtin_cpu_supports("avx2")) {
            mask |= CPU_AVX2;
            ieee_packed_add = __ieee_packed_add_avx2;
            ieee_to_01_slow_fn = __ieee_to_01_slow_avx2;
         }
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
   else if (name == island_begin_i)
      return sv_island_begin;
   else if (name == island_bind_i)
      return sv_island_bind_sig;
   else if (name == island_bind_gen_i)
      return sv_island_bind_gen_sig;
   else if (name == island_gen_key_i)
      return sv_island_set_gen_key;
   else if (name == island_attach_i)
      return sv_island_attach_sig;
   else if (name == island_apply_f_i)
      return sv_island_apply_forces;
   else if (name == island_add_dom_i)
      return sv_island_add_domain;
   else if (name == island_add_dom_w_i)
      return sv_island_add_domain_write;
   else if (name == island_finish_i)
      return sv_island_finish;
   else if (name == island_ext_read_i)
      return sv_island_mark_ext_read;
   else if (name == island_set_sparse_i)
      return sv_island_set_sparse;
   else if (name == island_sparse_write_i)
      return sv_island_sparse_write;
   else if (name == island_variant_a_i)
      return sv_island_set_variant_a;
   else if (name == island_bind_a_i)
      return sv_island_bind_sig_a;
   else if (name == island_add_dom_a_i)
      return sv_island_add_domain_a;

   for (const jit_intrinsic_t *it = intrinsic_list; it->name; it++) {
      if (it->ident == name)
         return it->entry;
   }

   return NULL;
}
