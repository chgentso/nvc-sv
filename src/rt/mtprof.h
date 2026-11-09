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

#ifndef _RT_MTPROF_H
#define _RT_MTPROF_H

// Run-time profile for multi-threading studies.  Enabled with the
// environment variable NVC_MT_PROFILE=1: every hook below is a single
// test of a global flag when it is off.  Time is attributed exclusively
// to the phase that is current: entering a phase charges the time since
// the last switch to the phase that was running.

#include "prim.h"
#include "util.h"

#include <stdint.h>

typedef enum {
   MTP_OTHER,       // Everything not listed below
   MTP_SCHED,       // Event queue extraction and scheduling
   MTP_DRIVERQ,     // Matured transactions: driver updates, timeouts
   MTP_UPD_DRV,     // Driving value updates
   MTP_UPD_EFF,     // Effective value updates
   MTP_NBA,         // Verilog non-blocking assignment region
   MTP_TASKQ,       // Run queue tasks that are not processes
   MTP_P_VHDL,      // VHDL processes
   MTP_P_SV_ALWAYS, // Verilog always processes
   MTP_P_SV_INIT,   // Verilog initial processes
   MTP_P_SV_FORK,   // Forked/spawned threads (class and testbench code)
   MTP_P_SV_OTHER,  // Other Verilog processes, conversions, final
   MTP_WATCH,       // Signal watch callbacks other than wave and VPI
   MTP_WAVE,        // Waveform dumping
   MTP_VPI,         // VPI and VHPI callbacks
   MTP_PHASECB,     // Phase callbacks other than wave and VPI
   MTP_NPHASES
} mtp_phase_t;

#define MTP_FIRST_PROC MTP_P_VHDL
#define MTP_LAST_PROC  MTP_P_SV_OTHER

typedef struct {
   bool     enabled;      // NVC_MT_PROFILE is set
   bool     on;           // Inside the simulation loop and enabled
   mtp_phase_t cur;
   uint64_t last;
   uint64_t ticks[MTP_NPHASES];
   uint64_t runs[MTP_NPHASES];   // Process runs and callbacks
   uint64_t events;              // Signal events
   uint64_t nupd_drv, nupd_eff;  // Nexus updates in the current cycle
   uint32_t cycle_procs;         // Process runs in the current cycle
} mtp_state_t;

extern mtp_state_t mtp;

static inline uint64_t mtp_clock(void)
{
#if defined __x86_64__
   return __builtin_ia32_rdtsc();
#else
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
#endif
}

static inline mtp_phase_t mtp_enter(mtp_phase_t phase)
{
   if (likely(!mtp.on))
      return MTP_OTHER;

   const uint64_t now = mtp_clock();
   const mtp_phase_t prev = mtp.cur;
   mtp.ticks[prev] += now - mtp.last;
   mtp.last = now;
   mtp.cur = phase;
   return prev;
}

#define MTP_LEAVE(prev) (void)mtp_enter(prev)

void mtp_init(void);
void mtp_tag_fn(void *fn, mtp_phase_t phase);
mtp_phase_t mtp_classify_fn(void *fn, mtp_phase_t dflt);
mtp_phase_t mtp_classify_proc(rt_proc_t *proc);
void mtp_start(void);               // Entering the simulation loop
void mtp_stop(void);                // Leaving it
void mtp_cycle_begin(void);
void mtp_cycle_end(bool is_delta);
void mtp_report(rt_scope_t *root);

#endif   // _RT_MTPROF_H
