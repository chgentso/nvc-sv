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
#include "rt/mtprof.h"
#include "rt/structs.h"
#include "tree.h"
#include "type.h"
#include "vlog/vlog-node.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NBUCKETS 12
#define MAX_TAGS 16

mtp_state_t mtp;

typedef struct {
   void        *fn;
   mtp_phase_t  phase;
} tag_t;

static tag_t    tags[MAX_TAGS];
static unsigned ntags;

typedef struct {
   uint64_t cycles[NBUCKETS];
   uint64_t proc_ticks[NBUCKETS];   // By runnable processes in the delta
   uint64_t nex_cycles[NBUCKETS];
   uint64_t upd_ticks[NBUCKETS];    // By nexuses updated in the delta
   uint64_t eff_cycles[NBUCKETS];
} hist_t;

static hist_t   hist;
static uint64_t n_deltas, n_steps, n_cycles;
static uint64_t tot_procs, tot_drv, tot_eff;
static uint64_t snap_proc, snap_upd;
static uint64_t wall_ns, tick_total, start_ns, start_tick;
static uint64_t max_procs, max_nex;

static const char *const phase_names[MTP_NPHASES] = {
   [MTP_OTHER]      = "other (loop, triggers, regions, final)",
   [MTP_SCHED]      = "event queue / scheduler",
   [MTP_DRIVERQ]    = "signal update: matured transactions",
   [MTP_UPD_DRV]    = "signal update: driving",
   [MTP_UPD_EFF]    = "signal update: effective",
   [MTP_NBA]        = "verilog NBA region",
   [MTP_TASKQ]      = "run queue non-process tasks",
   [MTP_P_VHDL]     = "process exec: VHDL",
   [MTP_P_SV_ALWAYS]= "process exec: SV always",
   [MTP_P_SV_INIT]  = "process exec: SV initial",
   [MTP_P_SV_FORK]  = "process exec: SV fork/class threads",
   [MTP_P_SV_OTHER] = "process exec: SV other (final, conv)",
   [MTP_WATCH]      = "signal watch callbacks (other)",
   [MTP_WAVE]       = "wave writing",
   [MTP_VPI]        = "VPI/VHPI callbacks",
   [MTP_PHASECB]    = "phase callbacks (other)",
};

static const char *const phase_keys[MTP_NPHASES] = {
   "other", "sched", "driverq", "upd_drv", "upd_eff", "nba", "taskq",
   "p_vhdl", "p_sv_always", "p_sv_initial", "p_sv_fork", "p_sv_other",
   "watch", "wave", "vpi", "phasecb",
};

void mtp_init(void)
{
   const char *env = getenv("NVC_MT_PROFILE");
   mtp.enabled = env != NULL && *env != '\0' && strcmp(env, "0") != 0;
}

void mtp_tag_fn(void *fn, mtp_phase_t phase)
{
   if (!mtp.enabled || ntags == MAX_TAGS)
      return;

   for (unsigned i = 0; i < ntags; i++) {
      if (tags[i].fn == fn)
         return;
   }

   tags[ntags++] = (tag_t){ fn, phase };
}

mtp_phase_t mtp_classify_fn(void *fn, mtp_phase_t dflt)
{
   for (unsigned i = 0; i < ntags; i++) {
      if (tags[i].fn == fn)
         return tags[i].phase;
   }

   return dflt;
}

mtp_phase_t mtp_classify_proc(rt_proc_t *proc)
{
   if (proc->wakeable.forked)
      return MTP_P_SV_FORK;

   switch (tree_kind(proc->where)) {
   case T_PROCESS:
      return MTP_P_VHDL;
   case T_VERILOG:
      switch (vlog_kind(tree_vlog(proc->where))) {
      case V_ALWAYS:  return MTP_P_SV_ALWAYS;
      case V_INITIAL: return MTP_P_SV_INIT;
      default:        return MTP_P_SV_OTHER;
      }
   default:
      return MTP_P_SV_OTHER;
   }
}

static uint64_t wall_now_ns(void)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

void mtp_start(void)
{
   if (!mtp.enabled || mtp.on)
      return;

   start_ns = wall_now_ns();
   start_tick = mtp.last = mtp_clock();
   mtp.cur = MTP_OTHER;
   mtp.on = true;
}

void mtp_stop(void)
{
   if (!mtp.on)
      return;

   (void)mtp_enter(MTP_OTHER);
   mtp.on = false;

   wall_ns += wall_now_ns() - start_ns;
   tick_total += mtp.last - start_tick;
}

static uint64_t proc_ticks(void)
{
   uint64_t sum = 0;
   for (int i = MTP_FIRST_PROC; i <= MTP_LAST_PROC; i++)
      sum += mtp.ticks[i];
   return sum;
}

static uint64_t upd_ticks(void)
{
   return mtp.ticks[MTP_DRIVERQ] + mtp.ticks[MTP_UPD_DRV]
      + mtp.ticks[MTP_UPD_EFF] + mtp.ticks[MTP_NBA];
}

static int bucket(uint64_t n)
{
   // 0, 1, 2-3, 4-7, ..., 512-1023, 1024+
   if (n == 0)
      return 0;
   const int b = 64 - __builtin_clzll(n);   // 1 for n == 1
   return b < NBUCKETS - 1 ? b : NBUCKETS - 1;
}

void mtp_cycle_begin(void)
{
   mtp.cycle_procs = 0;
   mtp.nupd_drv = mtp.nupd_eff = 0;
   (void)mtp_enter(mtp.cur);   // Charge the time so far
   snap_proc = proc_ticks();
   snap_upd = upd_ticks();
}

void mtp_cycle_end(bool is_delta)
{
   (void)mtp_enter(mtp.cur);

   const uint64_t dproc = proc_ticks() - snap_proc;
   const uint64_t dupd = upd_ticks() - snap_upd;

   const int bp = bucket(mtp.cycle_procs);
   hist.cycles[bp]++;
   hist.proc_ticks[bp] += dproc;

   const int bn = bucket(mtp.nupd_drv);
   hist.nex_cycles[bn]++;
   hist.upd_ticks[bn] += dupd;
   hist.eff_cycles[bucket(mtp.nupd_eff)]++;

   n_cycles++;
   if (is_delta)
      n_deltas++;
   else
      n_steps++;

   tot_procs += mtp.cycle_procs;
   tot_drv += mtp.nupd_drv;
   tot_eff += mtp.nupd_eff;
   max_procs = MAX(max_procs, mtp.cycle_procs);
   max_nex = MAX(max_nex, mtp.nupd_drv);
}

////////////////////////////////////////////////////////////////////////////////
// Static information about VHDL processes

typedef struct {
   bool shared, prot, file;
} proc_info_t;

static void scan_ref_cb(tree_t t, void *ctx)
{
   proc_info_t *info = ctx;

   if (!tree_has_ref(t))
      return;

   tree_t decl = tree_ref(t);
   switch (tree_kind(decl)) {
   case T_VAR_DECL:
      if (tree_flags(decl) & TREE_F_SHARED)
         info->shared = true;
      if (type_is_protected(tree_type(decl)))
         info->prot = true;
      break;
   case T_FILE_DECL:
      info->file = true;
      break;
   default:
      break;
   }
}

typedef struct {
   unsigned nvhdl, nshared, nprot, nfile, nclean;
} static_stats_t;

static void scan_scope(rt_scope_t *s, static_stats_t *st)
{
   for (int i = 0; i < s->procs.count; i++) {
      rt_proc_t *p = s->procs.items[i];
      if (tree_kind(p->where) != T_PROCESS)
         continue;

      proc_info_t info = {};
      tree_visit_only(p->where, scan_ref_cb, &info, T_REF);

      st->nvhdl++;
      st->nshared += info.shared;
      st->nprot += info.prot;
      st->nfile += info.file;
      st->nclean += !(info.shared || info.prot || info.file);
   }

   for (int i = 0; i < s->children.count; i++)
      scan_scope(s->children.items[i], st);
}

////////////////////////////////////////////////////////////////////////////////
// Reporting

static double amdahl(double p, int n)
{
   return 1.0 / ((1.0 - p) + p / n);
}

static const int thresholds[] = { 8, 32, 128 };
#define NTHRESH 3
static const int thread_counts[] = { 4, 8, 16 };

static const char *bucket_label(int b, char *buf, size_t len)
{
   if (b == 0)
      snprintf(buf, len, "0");
   else if (b == 1)
      snprintf(buf, len, "1");
   else if (b == NBUCKETS - 1)
      snprintf(buf, len, "%d+", 1 << (b - 1));
   else
      snprintf(buf, len, "%d-%d", 1 << (b - 1), (1 << b) - 1);
   return buf;
}

void mtp_report(rt_scope_t *root)
{
   if (!mtp.enabled || n_cycles == 0)
      return;

   const double ns_per_tick = tick_total ? (double)wall_ns / tick_total : 1.0;
   const double total_s = wall_ns / 1e9;

   uint64_t ptot = 0;
   for (int i = MTP_FIRST_PROC; i <= MTP_LAST_PROC; i++)
      ptot += mtp.ticks[i];

   const uint64_t utot = mtp.ticks[MTP_DRIVERQ] + mtp.ticks[MTP_UPD_DRV]
      + mtp.ticks[MTP_UPD_EFF] + mtp.ticks[MTP_NBA];

   FILE *f = stderr;
   fprintf(f, "\n==== NVC_MT_PROFILE ====\n");
   fprintf(f, "simulation loop wall time: %.3f s\n", total_s);
   fprintf(f, "time steps: %"PRIu64"  deltas: %"PRIu64"  cycles: %"PRIu64"\n",
           n_steps, n_deltas, n_cycles);
   fprintf(f, "events (signal changes): %"PRIu64"  (%.3g/s)   "
           "nexus driving updates: %"PRIu64"  effective updates: %"PRIu64"\n",
           mtp.events, total_s > 0 ? mtp.events / total_s : 0.0,
           tot_drv, tot_eff);
   fprintf(f, "process runs: %"PRIu64"  (%.3g/s)  mean per cycle: %.2f  "
           "max: %"PRIu64"\n", tot_procs,
           total_s > 0 ? tot_procs / total_s : 0.0,
           (double)tot_procs / n_cycles, max_procs);
   fprintf(f, "cycles per second: %.3g\n\n", total_s > 0 ? n_cycles / total_s : 0.0);

   fprintf(f, "%-42s %10s %7s %12s\n", "phase", "seconds", "%", "runs");
   for (int i = 0; i < MTP_NPHASES; i++) {
      const double s = mtp.ticks[i] * ns_per_tick / 1e9;
      fprintf(f, "%-42s %10.3f %6.1f%% %12"PRIu64"\n", phase_names[i], s,
              total_s > 0 ? 100.0 * s / total_s : 0.0, mtp.runs[i]);
   }
   fprintf(f, "%-42s %10.3f %6.1f%%\n", "process exec: total",
           ptot * ns_per_tick / 1e9,
           total_s > 0 ? 100.0 * ptot * ns_per_tick / 1e9 / total_s : 0.0);
   fprintf(f, "%-42s %10.3f %6.1f%%\n", "signal update: total (driverq+drv+eff+nba)",
           utot * ns_per_tick / 1e9,
           total_s > 0 ? 100.0 * utot * ns_per_tick / 1e9 / total_s : 0.0);

   fprintf(f, "\nhistogram (cycles) of runnable processes and of nexus "
           "driving updates per cycle\n");
   fprintf(f, "%-10s %10s %10s %10s %10s %10s\n", "bucket", "procs:cyc",
           "proc-s", "nexus:cyc", "upd-s", "effupd:cyc");
   for (int b = 0; b < NBUCKETS; b++) {
      char lab[16];
      fprintf(f, "%-10s %10"PRIu64" %10.3f %10"PRIu64" %10.3f %10"PRIu64"\n",
              bucket_label(b, lab, sizeof lab), hist.cycles[b],
              hist.proc_ticks[b] * ns_per_tick / 1e9, hist.nex_cycles[b],
              hist.upd_ticks[b] * ns_per_tick / 1e9, hist.eff_cycles[b]);
   }

   static_stats_t st = {};
   if (root != NULL)
      scan_scope(root, &st);

   fprintf(f, "\nVHDL processes (static, direct references only): %u total, "
           "%u shared-variable, %u protected-type, %u file, %u none\n",
           st.nvhdl, st.nshared, st.nprot, st.nfile, st.nclean);

   double p3[NTHRESH], p2[NTHRESH];
   fprintf(f, "\nparallelisable share (fraction of loop wall time)\n");
   fprintf(f, "%-8s %10s %10s  %s\n", ">=N", "step3:proc", "step2:upd",
           "Amdahl speedup @4/8/16 (step3 | step2 | both)");
   for (int t = 0; t < NTHRESH; t++) {
      const int b0 = bucket(thresholds[t]);
      uint64_t pt = 0, ut = 0;
      for (int b = b0; b < NBUCKETS; b++) {
         pt += hist.proc_ticks[b];
         ut += hist.upd_ticks[b];
      }
      const double tt = tick_total ? (double)tick_total : 1.0;
      p3[t] = pt / tt;
      p2[t] = ut / tt;
      fprintf(f, ">=%-6d %9.1f%% %9.1f%%  ", thresholds[t], 100 * p3[t],
              100 * p2[t]);
      for (int k = 0; k < 3; k++)
         fprintf(f, "%.2f/%.2f/%.2f%s", amdahl(p3[t], thread_counts[k]),
                 amdahl(p2[t], thread_counts[k]),
                 amdahl(p3[t] + p2[t], thread_counts[k]), k < 2 ? " " : "");
      fprintf(f, "\n");
   }
   fprintf(f, "======================\n");

   const char *json = getenv("NVC_MT_PROFILE_JSON");
   if (json == NULL)
      return;

   FILE *j = fopen(json, "w");
   if (j == NULL)
      return;

   fprintf(j, "{\n \"loop_s\": %.6f, \"steps\": %"PRIu64", \"deltas\": %"PRIu64
           ", \"cycles\": %"PRIu64", \"events\": %"PRIu64
           ", \"proc_runs\": %"PRIu64", \"drv_updates\": %"PRIu64
           ", \"eff_updates\": %"PRIu64",\n \"phases\": {", total_s, n_steps,
           n_deltas, n_cycles, mtp.events, tot_procs, tot_drv, tot_eff);
   for (int i = 0; i < MTP_NPHASES; i++)
      fprintf(j, "%s\"%s\": {\"s\": %.6f, \"runs\": %"PRIu64"}",
              i ? ", " : "", phase_keys[i], mtp.ticks[i] * ns_per_tick / 1e9,
              mtp.runs[i]);
   fprintf(j, "},\n \"hist\": [");
   for (int b = 0; b < NBUCKETS; b++) {
      char lab[16];
      fprintf(j, "%s{\"b\": \"%s\", \"proc_cycles\": %"PRIu64
              ", \"proc_s\": %.6f, \"nex_cycles\": %"PRIu64
              ", \"upd_s\": %.6f, \"eff_cycles\": %"PRIu64"}", b ? ", " : "",
              bucket_label(b, lab, sizeof lab), hist.cycles[b],
              hist.proc_ticks[b] * ns_per_tick / 1e9, hist.nex_cycles[b],
              hist.upd_ticks[b] * ns_per_tick / 1e9, hist.eff_cycles[b]);
   }
   fprintf(j, "],\n \"share\": [");
   for (int t = 0; t < NTHRESH; t++)
      fprintf(j, "%s{\"min\": %d, \"step3\": %.5f, \"step2\": %.5f}",
              t ? ", " : "", thresholds[t], p3[t], p2[t]);
   fprintf(j, "],\n \"vhdl_static\": {\"total\": %u, \"shared\": %u, "
           "\"protected\": %u, \"file\": %u, \"clean\": %u}\n}\n", st.nvhdl,
           st.nshared, st.nprot, st.nfile, st.nclean);
   fclose(j);
}
