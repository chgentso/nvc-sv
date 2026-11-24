//
//  Copyright (C) 2011-2026  Nick Gasson
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

#ifndef _RT_DEFS_H
#define _RT_DEFS_H

#include "prim.h"

#include <stdint.h>

#define RT_ALIGN_MASK    0x7
#define RT_MULTITHREADED 0

#define TIME_HIGH INT64_MAX  // Value of TIME'HIGH

#if RT_MULTITHREADED
#define RT_LOCK(x) SCOPED_LOCK(x)
#define MULTITHREADED_ONLY(x) x
#else
#define RT_LOCK(x)
#define MULTITHREADED_ONLY(x)
#endif

typedef void (*sig_event_fn_t)(uint64_t now, rt_signal_t *signal,
                               rt_watch_t *watch, void *user);
typedef void (*rt_event_fn_t)(rt_model_t *m, void *user);

typedef enum {
   R_MEMO      = (1 << 0),
   R_IDENT     = (1 << 1),
   R_COMPOSITE = (1 << 2),
   R_BLOB      = (1 << 3),
} res_flags_t;

#define NET_F_FORCED       (1 << 0)
#define NET_F_INOUT        (1 << 1)
#define NET_F_CACHE_EVENT  (1 << 2)
#define NET_F_R_IDENT      (1 << 3)
#define NET_F_PENDING      (1 << 4)
#define NET_F_HAS_INITIAL  (1 << 5)
#define NET_F_FAST_DRIVER  (1 << 6)
#define NET_F_EFFECTIVE    (1 << 7)
typedef uint8_t net_flags_t;

#define SIG_F_INIT_EVENT   (1 << 8)
#define SIG_F_STD_LOGIC    (1 << 9)
#define SIG_F_CACHE_EVENT  (1 << 10)
#define SIG_F_EVENT_FLAG   (1 << 11)
#define SIG_F_REGISTER     (1 << 12)
#define SIG_F_PACKED4      (1 << 13)
#define SIG_F_PACKED2      (1 << 14)
#define SIG_F_PACKED       (SIG_F_PACKED4 | SIG_F_PACKED2)
#define SIG_F_PACKED_DRIVEN (1 << 15)
#define SIG_F_FAST_NOOP    (1 << 16)
#define SIG_F_POLL_WATCHED (1 << 17)
#define SIG_F_FAST_DIRECT  (1 << 18)
#define SIG_F_NB_DEFERQ    (1 << 19)
#define SIG_F_ISLAND       (1 << 20)
#define SIG_F_EXT_READ     (1 << 21)
typedef uint32_t sig_flags_t;

#define PACK_SRC_SCALAR    (1 << 0)
#define PACK_SRC_VEC2      (1 << 1)
#define PACK_SRC_DST4      (1 << 2)
#define PACK_SRC_WHOLE     (1 << 3)
#define PACK_SRC_WIDTH_SH  8
#define PACK_SRC_WIDTH_MSK (0x7fu << PACK_SRC_WIDTH_SH)

typedef enum {
   SCOPE_ROOT,
   SCOPE_INSTANCE,
   SCOPE_PACKAGE,
   SCOPE_ARRAY,
   SCOPE_RECORD,
} rt_scope_kind_t;

typedef uint16_t delta_cycle_t;
#define DELTA_CYCLE_MAX UINT16_MAX

#endif  // _RT_DEFS_H
