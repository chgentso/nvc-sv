#include "vpi_test.h"

static int finished_in_cb = 0;

static PLI_INT32 on_valchg(p_cb_data cb)
{
   if (vpi_test_now() == 5 && !finished_in_cb) {
      finished_in_cb = 1;
      vpi_control(vpiFinish, 0);
   }
   return 0;
}

static PLI_INT32 end_of_sim(p_cb_data cb)
{
   check_int(vpi_test_now(), 5);
   check_int(finished_in_cb, 1);
   vpi_test_pass();
   return 0;
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   vpiHandle sig = vpi_test_lookup("vpi29.sig");
   s_cb_data vcb = {
      .reason = cbValueChange,
      .cb_rtn = on_valchg,
      .obj    = sig,
   };
   check_handle(vpi_register_cb(&vcb));
   return 0;
}

void vpi29_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
