#include "vhpi_test.h"

#include <string.h>

// The statement breakpoints of the nvc VPI extension (vpi_ext_nvc.h)
typedef void (*stmt_fn_t)(const char *file, int line, const char *process,
                          const char *scope, void *user_data);
void vpi_nvc_set_stmt_cb(stmt_fn_t fn, void *user_data);
int vpi_nvc_stmt_breakpoint_add(const char *file, int line);
int vpi_nvc_stmt_breakpoint_remove(const char *file, int line);
int vpi_nvc_stmt_step(int enable);

#define check_int(a, b) fail_unless((long)(a) == (long)(b))

typedef struct {
   int  line;
   long time;
   int  s;
   char process[32];
   char scope[32];
} hit_t;

static hit_t hits[16];
static int nhits;
static vhpiHandleT sig;

static void on_stmt(const char *file, int line, const char *process,
                    const char *scope, void *user_data)
{
   fail_if(strstr(file, "vhpi30.vhd") == NULL);

   vhpiTimeT t;
   vhpi_get_time(&t, NULL);

   vhpiValueT v = { .format = vhpiIntVal };
   fail_if(vhpi_get_value(sig, &v));

   if (nhits < 16) {
      hit_t *h = &hits[nhits];
      h->line = line;
      h->time = t.low;
      h->s = v.value.intg;
      strncpy(h->process, process ?: "", sizeof(h->process) - 1);
      strncpy(h->scope, scope ?: "", sizeof(h->scope) - 1);
   }
   nhits++;

   // Stepping: the next statement calls back too, whatever its line
   if (nhits == 1) {
      check_int(vpi_nvc_stmt_step(1), 1);
   }
   else if (nhits == 2) {
      check_int(vpi_nvc_stmt_step(0), 1);
   }
}

static void start_of_sim(const vhpiCbDataT *cb_data)
{
   vhpiHandleT root = VHPI_CHECK(vhpi_handle(vhpiRootInst, NULL));
   sig = VHPI_CHECK(vhpi_handle_by_name("s", root));
   vhpi_release_handle(root);

   vpi_nvc_set_stmt_cb(on_stmt, NULL);

   // The code has to be generated with --debug-stmts
   check_int(vpi_nvc_stmt_breakpoint_add("regress/vhpi30.vhd", 21), 1);
   check_int(vpi_nvc_stmt_breakpoint_add("vhpi30.vhd", 14), 1);
   check_int(vpi_nvc_stmt_breakpoint_add("vhpi30.vhd", 32), 1);
   check_int(vpi_nvc_stmt_breakpoint_add("vhpi30.vhd", 24), 1);
   check_int(vpi_nvc_stmt_breakpoint_add("other.vhd", 21), 1);
   check_int(vpi_nvc_stmt_breakpoint_remove("vhpi30.vhd", 24), 1);
   check_int(vpi_nvc_stmt_breakpoint_remove("vhpi30.vhd", 24), 0);
}

static void end_of_sim(const vhpiCbDataT *cb_data)
{
   vpi_nvc_set_stmt_cb(NULL, NULL);

   // line 24 was removed again, other.vhd never runs
   check_int(nhits, 4);

   check_int(hits[0].line, 21);
   check_int(hits[0].time, 1000000);
   check_int(hits[0].s, 0);
   fail_if(strstr(hits[0].process, ":p_a") == NULL);

   // The statement after it, found by stepping
   check_int(hits[1].line, 22);
   check_int(hits[1].time, 1000000);
   fail_if(strstr(hits[1].process, ":p_a") == NULL);

   // Called before the assignment: s still has the value of the first
   check_int(hits[2].line, 14);
   check_int(hits[2].time, 2000000);
   check_int(hits[2].s, 1);
   fail_if(strstr(hits[2].process, ":p_a") == NULL);
   fail_if(strstr(hits[2].scope, ":vhpi30") == NULL);

   check_int(hits[3].line, 32);
   check_int(hits[3].time, 3000000);
   check_int(hits[3].s, 2);
   fail_if(strstr(hits[3].process, ":p_b") == NULL);
}

void vhpi30_startup(void)
{
   vhpiCbDataT cb_data = {
      .reason = vhpiCbStartOfSimulation,
      .cb_rtn = start_of_sim,
   };
   VHPI_CHECK(vhpi_register_cb(&cb_data, 0));

   cb_data.reason = vhpiCbEndOfSimulation;
   cb_data.cb_rtn = end_of_sim;
   VHPI_CHECK(vhpi_register_cb(&cb_data, 0));
}
