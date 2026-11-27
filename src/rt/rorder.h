//
//  Copyright (C) 2026  Nick Gasson
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

#ifndef _RT_RORDER_H
#define _RT_RORDER_H

#include "prim.h"
#include "thread.h"

// Output of processes to stdout and stderr is held until the end of the
// delta cycle and then written in the order in which the processes start
// (see rorder.c)

typedef enum {
   REPORT_ORDER_IMMEDIATE,
   REPORT_ORDER_STABLE,
} report_order_t;

void rorder_enable(report_order_t mode);
void rorder_disable(void);
void rorder_flush_window(void);
int64_t rorder_report_begin(void);
void rorder_report_end(int64_t saved);

extern unsigned rorder_pending;
extern unsigned rorder_sv_pending;
extern __thread bool rorder_bypass;

// Called by the kernel at the end of each step that runs processes
static inline void rorder_window_end(void)
{
   if (unlikely(relaxed_load(&rorder_pending) > 0 || rorder_bypass))
      rorder_flush_window();
}

// Foreign code may write to stdout directly: what it and anything it
// calls writes appears as it happens, after what is held
static inline bool rorder_foreign_begin(void)
{
   const bool saved = rorder_bypass;
   if (unlikely(relaxed_load(&rorder_pending) > 0))
      rorder_flush_window();
   rorder_bypass = true;
   return saved;
}

static inline void rorder_foreign_end(bool saved)
{
   rorder_bypass = saved;
}

#endif  // _RT_RORDER_H
