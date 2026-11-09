#include "vpi_test.h"
#include "sv_vpi_user.h"

#include <string.h>

typedef struct {
   vpiHandle h;
   int       type;
   int       line;
} proc_t;

static proc_t procs[8];
static int nprocs;
static vpiHandle lv_var;
static vpiHandle init1;

static PLI_INT32 sample(p_cb_data cb)
{
   const int t = (int)(intptr_t)cb->user_data;

   if (t == 5) {
      // lv is 3 and the first initial procedure waits 5 more ns
      s_vpi_value v = { .format = vpiIntVal };
      vpi_get_value(lv_var, &v);
      check_int(v.value.integer, 3);

      check_int(vpi_get(vpiNvcProcState, init1), vpiNvcProcTimeout);

      s_vpi_time wt = { .type = vpiSimTime };
      check_int(vpi_nvc_get_wake_time(init1, &wt), 1);
      check_int(wt.low, 10000);   // In picoseconds
   }
   else if (t == 15) {
      s_vpi_value v = { .format = vpiIntVal };
      vpi_get_value(lv_var, &v);
      check_int(v.value.integer, 7);
   }
   else if (t == 25) {
      s_vpi_value v = { .format = vpiIntVal };
      vpi_get_value(lv_var, &v);
      check_int(v.value.integer, 9);

      // It has ended
      check_int(vpi_get(vpiNvcProcState, init1) & vpiNvcProcEnded,
                vpiNvcProcEnded);
      s_vpi_time wt = { .type = vpiSimTime };
      check_int(vpi_nvc_get_wake_time(init1, &wt), 0);
   }

   return 0;
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   vpiHandle mod = vpi_test_lookup("vpi20");
   check_int(vpi_get(vpiType, mod), vpiModule);

   vpiHandle it = vpi_iterate(vpiProcess, mod);
   check_error();
   check_handle(it);

   int ninit = 0, nalways = 0, nfinal = 0, ncomb = 0, nff = 0;
   vpiHandle p;
   while ((p = vpi_scan(it)) != NULL) {
      const int type = vpi_get(vpiType, p);
      const int line = vpi_get(vpiLineNo, p);
      check_string(vpi_get_str(vpiFile, p) != NULL ? "ok" : "none", "ok");

      vpiHandle parent = vpi_handle(vpiScope, p);
      check_handle(parent);
      check_string(vpi_get_str(vpiFullName, parent), "vpi20");
      vpiHandle parent2 = vpi_handle(vpiModule, p);
      check_handle(parent2);
      check_int(vpi_get(vpiType, parent2), vpiModule);

      fail_unless(vpi_get_str(vpiName, p) != NULL);
      fail_unless(strstr(vpi_get_str(vpiFullName, p), "vpi20.") != NULL);

      switch (type) {
      case vpiInitial:
         ninit++;
         if (line == 15)
            init1 = p;
         break;
      case vpiAlways:
         nalways++;
         switch (vpi_get(vpiAlwaysType, p)) {
         case vpiAlways: break;
         case vpiAlwaysComb: ncomb++; check_int(line, 32); break;
         case vpiAlwaysFF: nff++; check_int(line, 34); break;
         default: fail_unless(false);
         }
         break;
      case vpiFinal:
         nfinal++;
         break;
      default:
         vpi_test_fail(__FILE__, __LINE__, "unexpected type %d", type);
      }

      if (nprocs < 8) {
         procs[nprocs].h = p;
         procs[nprocs].type = type;
         procs[nprocs].line = line;
         nprocs++;
      }
   }

   check_int(ninit, 2);
   check_int(nalways, 4);
   check_int(nfinal, 1);
   check_int(ncomb, 1);
   check_int(nff, 1);

   // The same objects by type
   int n = 0;
   it = vpi_iterate(vpiInitial, mod);
   check_handle(it);
   while (vpi_scan(it) != NULL)
      n++;
   check_int(n, 2);

   check_handle(init1);

   // Variables of the block
   it = vpi_iterate(vpiVariables, init1);
   check_error();
   check_handle(it);
   lv_var = vpi_scan(it);
   check_handle(lv_var);
   fail_unless(strstr(vpi_get_str(vpiName, lv_var), "lv") != NULL);
   check_int(vpi_get(vpiSize, lv_var), 32);
   fail_unless(vpi_scan(it) == NULL);

   // The always procedure with a variable
   for (int i = 0; i < nprocs; i++) {
      if (procs[i].type == vpiAlways && procs[i].line == 26) {
         vpiHandle vit = vpi_iterate(vpiVariables, procs[i].h);
         check_handle(vit);
         vpiHandle kv = vpi_scan(it = vit);
         check_handle(kv);
         fail_unless(strstr(vpi_get_str(vpiName, kv), "k") != NULL);
      }
   }

   vpi_test_cb(cbAfterDelay, sample, 5000, (void *)(intptr_t)5);
   vpi_test_cb(cbAfterDelay, sample, 15000, (void *)(intptr_t)15);
   vpi_test_cb(cbAfterDelay, sample, 25000, (void *)(intptr_t)25);
   return 0;
}

static PLI_INT32 end_of_sim(p_cb_data cb)
{
   vpi_test_pass();
   return 0;
}

void vpi20_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
