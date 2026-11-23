#include "vpi_test.h"

#include <stdio.h>

static int next_times[8];
static int n_next_times = 0;
static int seen_valchg_at_5 = 0;
static int seen_delay_at_6 = 0;

static PLI_INT32 on_next_time(p_cb_data cb)
{
   PLI_UINT64 now = vpi_test_now();
   if (n_next_times < 8)
      next_times[n_next_times++] = (int)now;

   if (now < 10)
      vpi_test_cb(cbNextSimTime, on_next_time, 0, NULL);

   return 0;
}

static PLI_INT32 on_valchg(p_cb_data cb)
{
   PLI_UINT64 now = vpi_test_now();
   if (now == 5)
      seen_valchg_at_5 = 1;
   return 0;
}

static PLI_INT32 at_6ns(p_cb_data cb)
{
   check_int(vpi_test_now(), 6);
   check_int(seen_valchg_at_5, 1);
   seen_delay_at_6 = 1;
   vpi_control(vpiFinish, 0);
   return 0;
}

static PLI_INT32 end_of_sim(p_cb_data cb)
{
   check_int(vpi_test_now(), 6);
   check_int(seen_valchg_at_5, 1);
   check_int(seen_delay_at_6, 1);
   check_int(n_next_times >= 2, 1);
   check_int(next_times[0], 5);
   check_int(next_times[1], 6);
   vpi_test_pass();
   return 0;
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   vpiHandle sig = vpi_test_lookup("vpi28.sig");
   s_cb_data vcb = {
      .reason = cbValueChange,
      .cb_rtn = on_valchg,
      .obj    = sig,
   };
   check_handle(vpi_register_cb(&vcb));

   vpi_test_cb(cbNextSimTime, on_next_time, 0, NULL);
   vpi_test_cb(cbAfterDelay, at_6ns, 6, NULL);
   return 0;
}

void vpi28_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
