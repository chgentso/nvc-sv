//
//  Copyright (C) 2022-2024  Nick Gasson
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
#include "diag.h"
#include "option.h"
#include "thread.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <ctype.h>

typedef enum {
   OPT_KIND_UNDEF,
   OPT_KIND_INT,
   OPT_KIND_STRING,
   OPT_KIND_SIZE,
} option_kind_t;

typedef union {
   int     i;
   char   *s;
   size_t  z;
} optval_t;

typedef struct {
   option_kind_t kind;
   optval_t      value;
} option_t;

static option_t options[OPT_LAST_NAME];

static void opt_set_generic(opt_name_t name, option_kind_t kind,
                            optval_t value)
{
   assert(name < OPT_LAST_NAME);

   option_t *o = &(options[name]);

   if (o->kind == OPT_KIND_STRING)
      free(o->value.s);

   o->value = value;
   o->kind  = kind;
}

static inline optval_t opt_get_generic(opt_name_t name, option_kind_t kind)
{
   assert(name < OPT_LAST_NAME);

   const option_t *o = &(options[name]);
   if (likely(o->kind == kind))
      return o->value;
   else if (o->kind == OPT_KIND_UNDEF)
      fatal_trace("initial value for option %d not set", name);
   else
      fatal_trace("wrong option kind for %d", name);
}

void opt_set_int(opt_name_t name, int val)
{
   opt_set_generic(name, OPT_KIND_INT, (optval_t){ .i = val });
}

int opt_verilog_seed(void)
{
   // The random sequence starts from a fixed seed unless one is given on
   // the command line, so that a run can be repeated
   return opt_get_int(OPT_RANDOM_SEED_GIVEN) ? opt_get_int(OPT_RANDOM_SEED) : 0;
}

int opt_get_int(opt_name_t name)
{
   return opt_get_generic(name, OPT_KIND_INT).i;
}

void opt_set_size(opt_name_t name, size_t val)
{
   opt_set_generic(name, OPT_KIND_SIZE, (optval_t){ .z = val });
}

size_t opt_get_size(opt_name_t name)
{
   return opt_get_generic(name, OPT_KIND_SIZE).z;
}

void opt_set_str(opt_name_t name, const char *val)
{
   opt_set_generic(name, OPT_KIND_STRING,
                   (optval_t){ .s = val ? strdup(val) : NULL });
}

const char *opt_get_str(opt_name_t name)
{
   return opt_get_generic(name, OPT_KIND_STRING).s;
}

bool opt_get_verbose(opt_name_t name, const char *filter)
{
   const char *value = opt_get_str(name);
   if (value == NULL || *value == '\0')
      return false;
   else if (isdigit((int)*value))
      return true;
   else if (filter == NULL)
      return false;
   else if (value[0] == '^')
      return strcmp(value + 1, filter) == 0;
   else
      return strstr(filter, value) != NULL;
}

static int get_int_env(const char *var, int def)
{
   const char *env = getenv(var);
   if (env == NULL)
      return def;

   return atoi(env);
}

static int get_bool_env(const char *var, int def)
{
   const char *env = getenv(var);
   if (env == NULL || *env == '\0')
      return def;
   else if (strcasecmp(env, "0") == 0 || strcasecmp(env, "off") == 0
            || strcasecmp(env, "false") == 0 || strcasecmp(env, "no") == 0)
      return 0;
   else if (strcasecmp(env, "1") == 0 || strcasecmp(env, "on") == 0
            || strcasecmp(env, "true") == 0 || strcasecmp(env, "yes") == 0)
      return 1;
   else
      return atoi(env) != 0;
}

static int get_rtl_islands_env(void)
{
   const char *env = getenv("NVC_RTL_ISLANDS");
   if (env == NULL || *env == '\0' || strcasecmp(env, "hier") == 0)
      return RTL_ISLANDS_HIER;
   else if (strcasecmp(env, "off") == 0 || strcasecmp(env, "0") == 0)
      return RTL_ISLANDS_OFF;
   else if (strcasecmp(env, "module") == 0 || strcasecmp(env, "1") == 0)
      return RTL_ISLANDS_MODULE;
   else
      fatal("invalid value '%s' for NVC_RTL_ISLANDS "
            "(expected off, module, or hier)", env);
}

static int get_two_state_env(void)
{
   const char *env = getenv("NVC_2STATE");
   if (env == NULL || *env == '\0' || strcasecmp(env, "off") == 0
       || strcasecmp(env, "0") == 0)
      return TWO_STATE_OFF;
   else if (strcasecmp(env, "islands") == 0 || strcasecmp(env, "1") == 0)
      return TWO_STATE_ISLANDS;
   else if (strcasecmp(env, "all") == 0 || strcasecmp(env, "2") == 0)
      return TWO_STATE_ALL;
   else
      fatal("invalid value '%s' for NVC_2STATE "
            "(expected off, islands, or all)", env);
}

static int get_sim_threads_env(void)
{
   const char *env = getenv("NVC_THREADS");
   if (env == NULL || *env == '\0')
      return 1;
   else if (strcasecmp(env, "auto") == 0)
      return MIN(8, MAX(1, (int)nvc_nprocs()));

   char *eptr = NULL;
   const long val = strtol(env, &eptr, 10);
   if (eptr == env || *eptr != '\0' || val < 1 || val > MAX_THREADS)
      fatal("invalid value '%s' for NVC_THREADS "
            "(expected 1..%d or auto)", env, MAX_THREADS);
   return (int)val;
}

void set_default_options(void)
{
   opt_set_int(OPT_RT_STATS, 0);
   opt_set_int(OPT_RT_TRACE, get_int_env("NVC_MODEL_VERBOSE", 0));
   opt_set_str(OPT_PLI_TRACE, getenv("NVC_VHPI_VERBOSE"));
   opt_set_int(OPT_DUMP_LLVM, 0);
   opt_set_int(OPT_OPTIMISE, 2);
   opt_set_int(OPT_BOOTSTRAP, 0);
   opt_set_int(OPT_STOP_DELTA, 10000);
   opt_set_int(OPT_UNIT_TEST, 0);
   opt_set_int(OPT_MAKE_DEPS_ONLY, 0);
   opt_set_str(OPT_LOWER_VERBOSE, getenv("NVC_LOWER_VERBOSE"));
   opt_set_int(OPT_IGNORE_TIME, 0);
   opt_set_int(OPT_VERBOSE, 0);
   opt_set_int(OPT_MISSING_BODY, 1);
   opt_set_int(OPT_IEEE_WARNINGS, IEEE_WARNINGS_ON);
   opt_set_size(OPT_ARENA_SIZE, 1 << 24);
   opt_set_int(OPT_DUMP_ARRAYS, 0);
   opt_set_str(OPT_OBJECT_VERBOSE, getenv("NVC_OBJECT_VERBOSE"));
   opt_set_str(OPT_GC_VERBOSE, getenv("NVC_GC_VERBOSE") DEBUG_ONLY(?: "1"));
   opt_set_str(OPT_EVAL_VERBOSE, getenv("NVC_EVAL_VERBOSE"));
   opt_set_str(OPT_ELAB_VERBOSE, getenv("NVC_ELAB_VERBOSE"));
   opt_set_size(OPT_HEAP_SIZE, 1024 * 1024 * 1024);
   opt_set_int(OPT_GC_STRESS, 0 DEBUG_ONLY(|| get_int_env("NVC_GC_STRESS", 0)));
   opt_set_int(OPT_RELAXED, 0);
   opt_set_str(OPT_JIT_VERBOSE, getenv("NVC_JIT_VERBOSE"));
   opt_set_int(OPT_JIT_LOG, get_int_env("NVC_JIT_LOG", 0));
   opt_set_int(OPT_NO_SAVE, 0);
   opt_set_str(OPT_LLVM_VERBOSE, getenv("NVC_LLVM_VERBOSE"));
   opt_set_int(OPT_JIT_THRESHOLD, get_int_env("NVC_JIT_THRESHOLD", 100));
   opt_set_str(OPT_ASM_VERBOSE, getenv("NVC_ASM_VERBOSE"));
   opt_set_int(OPT_JIT_ASYNC, get_int_env("NVC_JIT_ASYNC", 1));
   opt_set_int(OPT_PERF_MAP, get_int_env("NVC_PERF_MAP", 0));
   opt_set_str(OPT_LIB_VERBOSE, getenv("NVC_LIB_VERBOSE"));
   opt_set_str(OPT_PSL_VERBOSE, getenv("NVC_PSL_VERBOSE"));
   opt_set_int(OPT_PSL_COMMENTS, 0);
   opt_set_int(OPT_NO_COLLAPSE, 0);
   opt_set_int(OPT_COVER_VERBOSE, get_int_env("NVC_COVER_VERBOSE", 0));
   opt_set_int(OPT_COVER_TIMESTAMP, get_int_env("NVC_COVER_TIMESTAMP", -1));
   opt_set_str(OPT_COVER_VERSION, getenv("NVC_COVER_VERSION"));
   opt_set_int(OPT_DRIVER_VERBOSE, get_int_env("NVC_DRIVER_VERBOSE", 0));
   opt_set_int(OPT_JIT_INTRINSICS, get_int_env("NVC_JIT_INTRINSICS", 1));
   opt_set_int(OPT_VECTOR_INTRINSICS, get_int_env("NVC_VECTOR_INTRINSICS", 1));
   opt_set_int(OPT_SHUFFLE_PROCS, 0);
   opt_set_int(OPT_PLI_DEBUG, opt_get_str(OPT_PLI_TRACE) != NULL);
   opt_set_int(OPT_SERVER_PORT, 8888);
   opt_set_int(OPT_STDERR_LEVEL, DIAG_DEBUG);
   opt_set_int(OPT_CHECK_SYNTHESIS, 0);
   opt_set_int(OPT_MISSING_WAIT, 1);
   opt_set_int(OPT_LAYOUT_VERBOSE, get_int_env("NVC_LAYOUT_VERBOSE", 0));
   opt_set_int(OPT_SINGLE_UNIT, 0);
   opt_set_int(OPT_PRESERVE_CASE, 0);
   opt_set_str(OPT_GVN_VERBOSE, getenv("NVC_GVN_VERBOSE"));
   opt_set_str(OPT_DCE_VERBOSE, getenv("NVC_DCE_VERBOSE"));
   opt_set_str(OPT_CFG_VERBOSE, getenv("NVC_CFG_VERBOSE"));
   opt_set_str(OPT_RA_VERBOSE, getenv("NVC_RA_VERBOSE"));
   opt_set_int(OPT_RANDOM_SEED, mix_bits_32(get_timestamp_us()));
   opt_set_int(OPT_RANDOM_SEED_GIVEN, 0);
   opt_set_int(OPT_ELAB_STATS, 0);
   opt_set_int(OPT_DEBUG_STMTS, get_int_env("NVC_DEBUG_STMTS", 0));
   opt_set_str(OPT_RELATIVE_PATH, NULL);
   opt_set_int(OPT_EXCL_VERBOSE, get_int_env("NVC_EXCL_VERBOSE", 0));
   opt_set_int(OPT_LOWER_MIR, get_int_env("NVC_LOWER_MIR", 0));
   opt_set_int(OPT_BLACK_BOX, 0);
   opt_set_int(OPT_LINT, 0);
   opt_set_str(OPT_BLACK_BOX_LIBS, NULL);
   opt_set_int(OPT_JIT_CACHE, get_bool_env("NVC_JIT_CACHE", 1));
   opt_set_str(OPT_JIT_CACHE_DIR, getenv("NVC_JIT_CACHE_DIR"));
   {
      const int limit_mb = get_int_env("NVC_JIT_CACHE_LIMIT_MB", 512);
      const size_t limit = limit_mb > 0 ? (size_t)limit_mb * 1024 * 1024 : 0;
      opt_set_size(OPT_JIT_CACHE_LIMIT, limit);
   }
   opt_set_int(OPT_RTL_ISLANDS, get_rtl_islands_env());
   opt_set_int(OPT_ISLAND_FAST, get_bool_env("NVC_ISLAND_FAST", 1));
   opt_set_int(OPT_DUMP_ISLANDS, get_bool_env("NVC_DUMP_ISLANDS", 0));
   opt_set_int(OPT_TWO_STATE, get_two_state_env());
   opt_set_int(OPT_ISLAND_SPEC, get_bool_env("NVC_ISLAND_SPEC", 0));
   opt_set_int(OPT_SIM_THREADS, get_sim_threads_env());
}

bool opt_black_box_allowed(const char *lib)
{
   if (!opt_get_int(OPT_BLACK_BOX))
      return false;

   const char *list = opt_get_str(OPT_BLACK_BOX_LIBS);
   if (list == NULL || *list == '\0')
      return true;

   const size_t len = strlen(lib);
   for (const char *p = list; *p; ) {
      const char *end = strchr(p, ',');
      const size_t n = end ? (size_t)(end - p) : strlen(p);
      if (n == len && strncasecmp(p, lib, n) == 0)
         return true;
      p = end ? end + 1 : p + n;
   }

   return false;
}
