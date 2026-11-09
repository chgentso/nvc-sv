#include "vpi_test.h"

#include <string.h>

typedef struct {
   int  line;
   long time;
   int  a, b;
} hit_t;

static hit_t hits[16];
static int nhits;
static vpiHandle ha, hb;

static int get_int(vpiHandle h)
{
   s_vpi_value v = { .format = vpiIntVal };
   vpi_get_value(h, &v);
   return v.value.integer;
}

static void on_stmt(const char *file, int line, const char *process,
                    const char *scope, void *user_data)
{
   fail_if(strstr(file, "vpi19.sv") == NULL);

   s_vpi_time t = { .type = vpiSimTime };
   vpi_get_time(NULL, &t);

   if (nhits < 16)
      hits[nhits] = (hit_t){ line, t.low, get_int(ha), get_int(hb) };
   nhits++;
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   vpiHandle top = vpi_scan(vpi_iterate(vpiModule, NULL));
   fail_if(top == NULL);
   ha = vpi_handle_by_name("a", top);
   hb = vpi_handle_by_name("b", top);
   fail_if(ha == NULL || hb == NULL);

   vpi_nvc_set_stmt_cb(on_stmt, NULL);

   // The code has to be generated with --debug-stmts
   check_int(vpi_nvc_stmt_breakpoint_add("regress/vpi19.sv", 14), 1);
   check_int(vpi_nvc_stmt_breakpoint_add("vpi19.sv", 16), 1);
   check_int(vpi_nvc_stmt_breakpoint_add("vpi19.sv", 21), 1);
   check_int(vpi_nvc_stmt_breakpoint_add("vpi19.sv", 11), 1);
   check_int(vpi_nvc_stmt_breakpoint_remove("vpi19.sv", 11), 1);
   return 0;
}

static PLI_INT32 end_of_sim(p_cb_data cb)
{
   vpi_nvc_set_stmt_cb(NULL, NULL);

   check_int(nhits, 4);

   // Before the statement runs: a and b have their old values
   check_int(hits[0].line, 14);
   check_int(hits[0].time, 1);
   check_int(hits[0].a, 0);

   check_int(hits[1].line, 16);
   check_int(hits[1].time, 2);
   check_int(hits[1].a, 1);

   check_int(hits[2].line, 21);
   check_int(hits[2].time, 3);
   check_int(hits[2].b, 0);

   check_int(hits[3].line, 21);
   check_int(hits[3].time, 6);
   check_int(hits[3].b, 1);

   vpi_test_pass();
   return 0;
}

void vpi19_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
