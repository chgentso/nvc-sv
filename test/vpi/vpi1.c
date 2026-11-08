#include "vpi_test.h"

#include <string.h>

static int start_count, end_count;
static bool finish_called;
static vpiHandle delay_cb;

static PLI_INT32 end_of_sim(p_cb_data cb)
{
   end_count++;

   fail_unless(start_count == 1);
   fail_unless(end_count == 1);
   fail_unless(finish_called);

   // vpi_control(vpiFinish) was called at 5 ns = 5000 ps
   check_int(vpi_test_now(), 5000);

   // The callback object of a callback which has happened is freed
   fail_unless(vpi_remove_cb(delay_cb) == 0);
   vpi_chk_error(NULL);

   vpi_test_pass();
   return 0;
}

static PLI_INT32 after_delay(p_cb_data cb)
{
   fail_unless(cb->reason == cbAfterDelay);
   fail_unless(cb->user_data == (PLI_BYTE8 *)&finish_called);
   check_int(vpi_test_now(), 5000);

   s_vpi_time t = { .type = vpiScaledRealTime };
   vpi_get_time(NULL, &t);
   check_error();
   // With a NULL object the time is in simulation time units (38.17)
   fail_unless(t.real == 5000.0);

   vpiHandle top = vpi_test_lookup("vpi1");
   t.type = vpiScaledRealTime;
   vpi_get_time(top, &t);
   // Scaled to the time unit of the module
   fail_unless(t.real == 5.0);

   finish_called = true;
   vpi_control(vpiFinish, 0);
   check_error();
   return 0;
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   start_count++;
   fail_unless(cb->reason == cbStartOfSimulation);
   check_int(vpi_test_now(), 0);

   vpiHandle top = vpi_test_lookup("vpi1");
   check_int(vpi_get(vpiTimeUnit, top), -9);
   check_int(vpi_get(vpiTimePrecision, top), -12);

   // Simulation time precision with a NULL handle
   check_int(vpi_get(vpiTimePrecision, NULL), -12);

   s_vpi_vlog_info info;
   fail_unless(vpi_get_vlog_info(&info));
   check_error();

   fail_if(info.product == NULL || info.product[0] == '\0');
   fail_if(info.version == NULL || info.version[0] == '\0');

   // The plusargs from the command line are in argv
   bool found = false;
   for (int i = 0; i < info.argc; i++) {
      if (strcmp(info.argv[i], "+vpi1+arg=42") == 0)
         found = true;
   }
   fail_unless(found);

   vpi_printf("product %s version %s\n", info.product, info.version);

   delay_cb = vpi_test_cb(cbAfterDelay, after_delay, 5000, &finish_called);
   return 0;
}

void vpi1_startup(void)
{
   // Startup routines register callbacks; other VPI access happens once
   // simulation starts
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
