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
#include "hash.h"
#include "thread.h"
#include "rt/crv.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Settings read from the environment and the per call site counters.
// Nothing here is read back by the solver: a call gives the same answer
// whatever the counters hold.

////////////////////////////////////////////////////////////////////////////////
// Environment

static const char *const engine_names[] = {
   "auto", "legacy", "dfs", "reject", "bdd", "sat"
};

static crv_engine_t engine = CRV_ENGINE_AUTO;
static bool         decompose = true;
static unsigned     bdd_nodes = 32768;
static bool         env_read;
static nvc_lock_t   env_lock;

static void read_env(void)
{
   SCOPED_LOCK(env_lock);

   if (env_read)
      return;

   const char *e = getenv("NVC_CRV_ENGINE");
   if (e != NULL && *e) {
      bool found = false;
      for (size_t i = 0; i < ARRAY_LEN(engine_names); i++) {
         if (strcmp(e, engine_names[i]) == 0) {
            engine = (crv_engine_t)i;
            found = true;
         }
      }

      if (!found)
         fprintf(stderr, "NVC_CRV_ENGINE: unknown engine '%s', expecting "
                 "legacy, dfs, reject, bdd, sat or auto\n", e);
      else if (!crv_engine_available(engine))
         fprintf(stderr, "NVC_CRV_ENGINE=%s: this stage is not available "
                 "yet, using the search of the dfs engine\n", e);
   }

   const char *d = getenv("NVC_CRV_DECOMPOSE");
   if (d != NULL && strcmp(d, "0") == 0)
      decompose = false;
   if (d != NULL && engine != CRV_ENGINE_LEGACY)
      fprintf(stderr, "NVC_CRV_DECOMPOSE has no effect: slices and masks are "
              "solved by the bdd stage, only NVC_CRV_ENGINE=legacy splits "
              "variables\n");

   const char *n = getenv("NVC_CRV_BDD_NODES");
   if (n != NULL && *n) {
      char *end;
      const long v = strtol(n, &end, 10);
      if (*end != '\0' || v < 0 || v > 100000000)
         fprintf(stderr, "NVC_CRV_BDD_NODES: expecting a number of nodes, "
                 "0 to 100000000, not '%s'\n", n);
      else
         bdd_nodes = v;
   }

   env_read = true;
}

crv_engine_t crv_engine(void)
{
   if (unlikely(!env_read))
      read_env();
   return engine;
}

void crv_set_engine(crv_engine_t e)
{
   if (!env_read)
      read_env();
   engine = e;
}

bool crv_engine_available(crv_engine_t e)
{
   // Legacy and dfs are the same search, reject and bdd are the stages on
   // their own and auto is the stages in turn
   return e == CRV_ENGINE_AUTO || e == CRV_ENGINE_LEGACY
      || e == CRV_ENGINE_DFS || e == CRV_ENGINE_REJECT
      || e == CRV_ENGINE_BDD;
}

const char *crv_engine_name(crv_engine_t e)
{
   return engine_names[e];
}

bool crv_decompose_enabled(void)
{
   if (unlikely(!env_read))
      read_env();
   return decompose;
}

void crv_set_decompose(bool on)
{
   if (!env_read)
      read_env();
   decompose = on;
}

unsigned crv_bdd_nodes(void)
{
   if (unlikely(!env_read))
      read_env();
   return bdd_nodes;
}

void crv_set_bdd_nodes(unsigned nodes)
{
   if (!env_read)
      read_env();
   bdd_nodes = nodes;
}

////////////////////////////////////////////////////////////////////////////////
// Counters

typedef struct {
   char     *site;
   uint64_t  calls, comps, loose, freevars, multi, decomposed, soft;
   uint64_t  fail, giveup;
   uint64_t  comp_stage[CRV_STAGE_COUNT];
   uint64_t  call_stage[CRV_STAGE_COUNT];
   uint64_t  declined[3];
   uint64_t  build_ns, solve_ns;
   uint64_t  bdd_declined[CRV_BDD_REASONS];
   uint64_t  bdd_build_ns, bdd_sample_ns, bdd_bytes;
} site_t;

static nvc_lock_t        stats_lock;
static shash_t          *sites;
static site_t          **site_list;
static unsigned          nsites, csites;
static int               mode = -1;       // -1 unknown, 0 off, 1 table, 2 file
static bool              forced;
static char             *stats_file;
static __thread char    *cur_site;
static __thread crv_call_stat_t last;
static bool              registered;


static void read_stats_env(void)
{
   if (mode >= 0)
      return;

   const char *e = getenv("NVC_CRV_STATS");
   if (e == NULL || *e == '\0' || strcmp(e, "0") == 0)
      mode = 0;
   else if (strcmp(e, "1") == 0)
      mode = 1;
   else {
      mode = 2;
      stats_file = xstrdup(e);
   }
}

bool crv_stats_enabled(void)
{
   if (unlikely(mode < 0)) {
      SCOPED_LOCK(stats_lock);
      read_stats_env();
   }
   return mode > 0 || forced;
}

void crv_stats_force(bool on)
{
   forced = on;
}

void crv_stats_site(const char *site)
{
   free(cur_site);
   cur_site = site ? xstrdup(site) : NULL;
}

const crv_call_stat_t *crv_stats_last(void)
{
   return &last;
}

const char *crv_bdd_reason_name(crv_bdd_reason_t reason)
{
   static const char *const names[CRV_BDD_REASONS] = {
      "randc", "guarded-dist", "dists", "bits", "size", "unique",
      "nonlinear", "gates", "budget", "check"
   };
   return names[reason];
}

const char *crv_stage_name(crv_stage_t stage)
{
   static const char *const names[CRV_STAGE_COUNT] = {
      "none", "reject", "bdd", "dfs", "sat", "mix"
   };
   return names[stage];
}

void crv_stats_record(const crv_call_stat_t *s)
{
   last = *s;

   if (mode <= 0)
      return;

   SCOPED_LOCK(stats_lock);

   if (!registered) {
      registered = true;
      atexit(crv_stats_write);
   }

   const char *name = cur_site ? cur_site : "?";
   if (sites == NULL)
      sites = shash_new(64);

   site_t *st = shash_get(sites, name);
   if (st == NULL) {
      st = xcalloc(sizeof(site_t));
      st->site = xstrdup(name);
      shash_put(sites, st->site, st);
      if (nsites == csites) {
         csites = csites ? csites * 2 : 32;
         site_list = xrealloc_array(site_list, csites, sizeof(site_t *));
      }
      site_list[nsites++] = st;
   }

   st->calls++;
   st->comps += s->comps;
   st->loose += s->loose;
   st->freevars += s->freevars;
   st->multi += s->maxfree > 1;
   st->decomposed += s->decomposed;
   st->soft += s->soft;
   st->fail += !s->ok;
   st->giveup += s->giveup;
   for (int i = 0; i < CRV_STAGE_COUNT; i++)
      st->comp_stage[i] += s->stage[i];
   st->call_stage[s->top]++;
   for (int i = 0; i < 3; i++)
      st->declined[i] += s->declined[i];
   st->build_ns += s->build_ns;
   st->solve_ns += s->solve_ns;
   for (int i = 0; i < CRV_BDD_REASONS; i++)
      st->bdd_declined[i] += s->bdd_declined[i];
   st->bdd_build_ns += s->bdd_build_ns;
   st->bdd_sample_ns += s->bdd_sample_ns;
   if (s->bdd_bytes > st->bdd_bytes)
      st->bdd_bytes = s->bdd_bytes;
}

void crv_stats_reset(void)
{
   SCOPED_LOCK(stats_lock);

   for (unsigned i = 0; i < nsites; i++) {
      free(site_list[i]->site);
      free(site_list[i]);
   }
   if (sites != NULL) {
      shash_free(sites);
      sites = NULL;
   }
   nsites = 0;
   memset(&last, 0, sizeof(last));
}

static int site_cmp(const void *a, const void *b)
{
   const site_t *x = *(site_t *const *)a, *y = *(site_t *const *)b;
   return x->calls < y->calls ? 1 : x->calls > y->calls ? -1
      : strcmp(x->site, y->site);
}

// Column order of the tab separated lines, fixed so that scripts can rely
// on it: site, calls, comps, loose, freevars, multi, decomposed, soft, fail,
// giveup, then the components answered by each stage (none, reject, bdd, dfs,
// sat, mix), the calls whose last stage was each of those, the declines
// (prefilter, budget, exhausted), build and solve time in microseconds, then
// the declines of the bdd stage by reason (crv_bdd_reason_t), its build and
// sample time in microseconds and the most memory a build held in bytes
static void write_sites(void)
{
   SCOPED_LOCK(stats_lock);

   if (nsites == 0)
      return;

   qsort(site_list, nsites, sizeof(site_t *), site_cmp);

   if (mode == 2) {
      FILE *f = fopen(stats_file, "a");
      if (f == NULL) {
         fprintf(stderr, "crv: cannot append to %s\n", stats_file);
         return;
      }

      for (unsigned i = 0; i < nsites; i++) {
         const site_t *st = site_list[i];
         fprintf(f, "%s\t%"PRIu64"\t%"PRIu64"\t%"PRIu64"\t%"PRIu64"\t%"PRIu64
                 "\t%"PRIu64"\t%"PRIu64"\t%"PRIu64"\t%"PRIu64,
                 st->site, st->calls, st->comps, st->loose, st->freevars,
                 st->multi, st->decomposed, st->soft, st->fail, st->giveup);
         for (int k = 0; k < CRV_STAGE_COUNT; k++)
            fprintf(f, "\t%"PRIu64, st->comp_stage[k]);
         for (int k = 0; k < CRV_STAGE_COUNT; k++)
            fprintf(f, "\t%"PRIu64, st->call_stage[k]);
         for (int k = 0; k < 3; k++)
            fprintf(f, "\t%"PRIu64, st->declined[k]);
         fprintf(f, "\t%"PRIu64"\t%"PRIu64, st->build_ns / 1000,
                 st->solve_ns / 1000);
         for (int k = 0; k < CRV_BDD_REASONS; k++)
            fprintf(f, "\t%"PRIu64, st->bdd_declined[k]);
         fprintf(f, "\t%"PRIu64"\t%"PRIu64"\t%"PRIu64"\n",
                 st->bdd_build_ns / 1000, st->bdd_sample_ns / 1000,
                 st->bdd_bytes);
      }
      fclose(f);
      return;
   }

   fprintf(stderr, "\ncrv statistics (calls per call site; stage columns "
           "count calls whose last stage was that one)\n");
   fprintf(stderr, "%-34s %8s %6s %6s %5s %5s %5s %6s | %7s %7s %7s %7s %7s "
           "| %9s %9s\n", "site", "calls", "comps", "multi", "decmp", "soft",
           "fail", "giveup", "none", "reject", "bdd", "dfs", "sat",
           "build us", "solve us");
   for (unsigned i = 0; i < nsites; i++) {
      const site_t *st = site_list[i];

      // The end of a long path is the part that identifies the call
      const size_t len = strlen(st->site);
      const char *shown = len > 34 ? st->site + len - 34 : st->site;
      fprintf(stderr, "%-34s %8"PRIu64" %6"PRIu64" %6"PRIu64" %5"PRIu64
              " %5"PRIu64" %5"PRIu64" %6"PRIu64" | %7"PRIu64" %7"PRIu64
              " %7"PRIu64" %7"PRIu64" %7"PRIu64" | %9"PRIu64" %9"PRIu64"\n",
              shown, st->calls, st->comps, st->multi, st->decomposed,
              st->soft, st->fail, st->giveup,
              st->call_stage[CRV_STAGE_NONE], st->call_stage[CRV_STAGE_REJECT],
              st->call_stage[CRV_STAGE_BDD], st->call_stage[CRV_STAGE_DFS],
              st->call_stage[CRV_STAGE_SAT],
              st->build_ns / 1000, st->solve_ns / 1000);
   }
}

void crv_stats_write(void)
{
   write_sites();
   crv_stats_reset();
}
