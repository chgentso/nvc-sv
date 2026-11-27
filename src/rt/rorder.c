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

#include "util.h"
#include "jit/jit.h"
#include "printf.h"
#include "rt/model.h"
#include "rt/rorder.h"
#include "thread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The LRM leaves the order in which the processes of a delta cycle run
// unspecified, and an RTL island runs the processes fused into it at
// one point of the cycle.  So that the reports, assertion messages and
// TEXTIO output of a simulation are the same however its processes are
// scheduled, what a VHDL process writes to stdout or stderr is held and
// written at the end of the step, sorted by the order in which the
// processes start (the order of their declarations, see reset_scope),
// then in the order it was written.
//
// A Verilog process may see what another one did earlier in the same
// step, so its output is held the same way but written before the next
// Verilog process runs: the output of Verilog processes keeps the order
// in which they ran.  Anything else written in the meantime, such as the
// output of a VPI or VHPI callback, foreign code or the kernel, first
// writes what is held: it keeps its place relative to the processes that
// ran before it, and so does a fatal error.  A report that stops the
// simulation is held like any other: the processes of the delta cycle
// still run and their output is written in the same order.

#define KEY_LAST INT64_MAX   // Unknown process on a worker thread

typedef struct {
   int64_t   key;
   uint64_t  seq;
   FILE     *file;
   size_t    off;
   size_t    len;
} rorder_ent_t;

typedef struct _rorder_buf rorder_buf_t;

struct _rorder_buf {
   nvc_lock_t     lock;
   rorder_ent_t  *ents;
   unsigned       nents;
   unsigned       maxents;
   char          *text;
   size_t         len;
   size_t         max;
   rorder_buf_t  *next;
};

typedef struct {
   rorder_ent_t *ent;
   rorder_buf_t *buf;
} rorder_ref_t;

unsigned        rorder_pending = 0;
unsigned        rorder_sv_pending = 0;
__thread bool   rorder_bypass = false;

static __thread rorder_buf_t *my_buf = NULL;
static __thread int64_t       report_key = -1;
static rorder_buf_t          *all_bufs = NULL;
static nvc_lock_t             bufs_lock = 0;
static uint64_t               next_seq = 0;
static bool                   enabled = false;

static inline bool is_main_thread(void)
{
   return thread_attached() && thread_id() == 0;
}

static rorder_buf_t *get_buf(void)
{
   if (likely(my_buf != NULL))
      return my_buf;

   rorder_buf_t *b = xcalloc(sizeof(rorder_buf_t));

   SCOPED_LOCK(bufs_lock);
   b->next = all_bufs;
   all_bufs = b;

   return (my_buf = b);
}

static void append(int64_t key, FILE *f, const char *buf, size_t len)
{
   rorder_buf_t *b = get_buf();

   SCOPED_LOCK(b->lock);

   if (b->len + len > b->max) {
      b->max = MAX(b->max * 2, MAX(b->len + len, 4096));
      b->text = xrealloc(b->text, b->max);
   }

   memcpy(b->text + b->len, buf, len);

   rorder_ent_t *last = b->nents > 0 ? &(b->ents[b->nents - 1]) : NULL;
   if (last != NULL && last->key == key && last->file == f
       && last->off + last->len == b->len)
      last->len += len;   // Continues the previous write
   else {
      if (b->nents == b->maxents) {
         b->maxents = MAX(b->maxents * 2, 64);
         b->ents = xrealloc_array(b->ents, b->maxents, sizeof(rorder_ent_t));
      }

      b->ents[b->nents++] = (rorder_ent_t){
         .key  = key,
         .seq  = relaxed_add(&next_seq, 1),
         .file = f,
         .off  = b->len,
         .len  = len,
      };

      relaxed_add(&rorder_pending, 1);
   }

   b->len += len;
}

static int ref_cmp(const void *a, const void *b)
{
   const rorder_ent_t *ea = ((const rorder_ref_t *)a)->ent;
   const rorder_ent_t *eb = ((const rorder_ref_t *)b)->ent;

   if (ea->key != eb->key)
      return ea->key < eb->key ? -1 : 1;
   else if (ea->seq != eb->seq)
      return ea->seq < eb->seq ? -1 : 1;
   else
      return 0;
}

static void flush_all(void)
{
   // Only the main thread writes held output, at the points where the
   // worker threads are idle or have finished adding to their buffers

   SCOPED_LOCK(bufs_lock);

   unsigned total = 0;
   for (rorder_buf_t *b = all_bufs; b != NULL; b = b->next) {
      nvc_lock(&(b->lock));
      total += b->nents;
   }

   if (total > 0) {
      rorder_ref_t *refs = xmalloc_array(total, sizeof(rorder_ref_t));
      unsigned n = 0;
      for (rorder_buf_t *b = all_bufs; b != NULL; b = b->next) {
         for (unsigned i = 0; i < b->nents; i++)
            refs[n++] = (rorder_ref_t){ &(b->ents[i]), b };
      }

      if (n > 1)
         qsort(refs, n, sizeof(rorder_ref_t), ref_cmp);

      FILE *last = NULL;
      for (unsigned i = 0; i < n; i++) {
         const rorder_ent_t *e = refs[i].ent;
         if (last != NULL && last != e->file)
            fflush(last);   // stdout and stderr are often the same file
         fwrite(refs[i].buf->text + e->off, e->len, 1, e->file);
         last = e->file;
      }

      fflush(stdout);
      fflush(stderr);

      free(refs);
   }

   for (rorder_buf_t *b = all_bufs; b != NULL; b = b->next) {
      b->nents = 0;
      b->len = 0;
      nvc_unlock(&(b->lock));
   }

   relaxed_store(&rorder_pending, 0);
   relaxed_store(&rorder_sv_pending, 0);
}

static bool rorder_capture(FILE *f, const char *buf, size_t len)
{
   if (rorder_bypass)
      return false;

   int64_t key = report_key;
   bool verilog = false;
   if (key < 0)
      key = model_output_key(&verilog);

   if (key < 0) {
      if (!is_main_thread())
         key = KEY_LAST;
      else {
         // Keeps its place after the output of the processes that ran
         // before it
         if (relaxed_load(&rorder_pending) > 0)
            flush_all();
         return false;
      }
   }

   append(key, f, buf, len);

   if (verilog)
      relaxed_store(&rorder_sv_pending, 1);

   return true;
}

static void rorder_urgent(bool begin)
{
   // What is written until the end of an urgent message appears after
   // everything held so far and is not held itself
   if (begin && is_main_thread() && relaxed_load(&rorder_pending) > 0)
      flush_all();

   rorder_bypass = begin && enabled;
}

void rorder_flush_window(void)
{
   if (!is_main_thread())
      return;

   if (relaxed_load(&rorder_pending) > 0)
      flush_all();

   rorder_bypass = false;
}

void rorder_enable(report_order_t mode)
{
   if (mode == REPORT_ORDER_STABLE) {
      enabled = true;
      nvc_set_output_capture(rorder_capture, rorder_urgent);
   }
   else
      rorder_disable();
}

void rorder_disable(void)
{
   if (relaxed_load(&rorder_pending) > 0 && is_main_thread())
      flush_all();

   enabled = false;
   rorder_bypass = false;
   nvc_set_output_capture(NULL, NULL);
}

int64_t rorder_report_begin(void)
{
   // A report from a process fused into an RTL island runs with no
   // active process: its stack trace names the process
   const int64_t saved = report_key;

   bool verilog;
   if (enabled && model_output_key(&verilog) < 0) {
      ident_t name = jit_last_island_proc();
      if (name != NULL)
         report_key = model_fused_proc_key(name);
   }

   return saved;
}

void rorder_report_end(int64_t saved)
{
   report_key = saved;
}
