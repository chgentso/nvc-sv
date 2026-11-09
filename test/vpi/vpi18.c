#include "vpi_test.h"

static int ncycles;
static struct { long time; int delta; } seen[16];
static int stepping;

static PLI_INT32 end_of_delta(p_cb_data cb)
{
   s_vpi_time t = { .type = vpiSimTime };
   vpi_get_time(NULL, &t);

   if (ncycles < 16) {
      seen[ncycles].time = t.low;
      seen[ncycles].delta = vpi_nvc_get_delta();
   }
   ncycles++;

   // Callbacks happen once: register again for the next cycle
   vpi_test_cb(cbNvcEndOfDelta, end_of_delta, 0, NULL);
   return 0;
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   check_int(vpi_nvc_get_delta(), 0);
   vpi_test_cb(cbNvcEndOfDelta, end_of_delta, 0, NULL);
   stepping = 1;
   return 0;
}

static PLI_INT32 end_of_sim(p_cb_data cb)
{
   fail_unless(stepping);

   // Every cycle is reported once, in order, with the delta number counting
   // from zero in each time step: initialisation, then the cycles at 10 ns
   // (a, b, c change) and at 20 ns
   int at10 = 0, at20 = 0;
   for (int i = 0; i < ncycles && i < 16; i++) {
      if (seen[i].time == 10000000) {
         check_int(seen[i].delta, at10);
         at10++;
      }
      else if (seen[i].time == 20000000) {
         check_int(seen[i].delta, at20);
         at20++;
      }
   }
   check_int(at10, 4);
   check_int(at20, 4);

   vpi_test_pass();
   return 0;
}

void vpi18_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
