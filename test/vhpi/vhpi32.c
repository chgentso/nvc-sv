// Registers value change callbacks on slices of a VHDL signal driven by Verilog, at start of simulation and in mid-run.

#include "vhpi_test.h"

#include <stdio.h>
#include <assert.h>

static int changes = 0;

static void ab_value_change(const vhpiCbDataT *cb_data)
{
   char buf[16];
   vhpiValueT value = {
      .format   = vhpiBinStrVal,
      .bufSize  = sizeof(buf),
      .value.str = (vhpiCharT *)buf,
   };
   VHPI_CHECK(vhpi_get_value(cb_data->obj, &value));

   changes++;
}

static void mid_run(const vhpiCbDataT *cb_data);

static void start_of_sim(const vhpiCbDataT *cb_data)
{
   vhpiHandleT ab = VHPI_CHECK(vhpi_handle_by_name(":vhpi32:u_ram:ab", NULL));

   vhpiCbDataT cb = {
      .reason = vhpiCbValueChange,
      .cb_rtn = ab_value_change,
      .obj    = ab,
   };
   VHPI_CHECK(vhpi_register_cb(&cb, 0));

   vhpi_release_handle(ab);

   static vhpiTimeT delay = { .low = 100000000 };   // 100 ns
   vhpiCbDataT mid_cb = {
      .reason = vhpiCbAfterDelay,
      .cb_rtn = mid_run,
      .time   = &delay,
   };
   VHPI_CHECK(vhpi_register_cb(&mid_cb, 0));
}

static void mid_run(const vhpiCbDataT *cb_data)
{
   // Watching a slice of a signal that was already split and is driven
   // from Verilog
   vhpiHandleT ab = VHPI_CHECK(vhpi_handle_by_name(":vhpi32:u_tap:ab", NULL));

   vhpiCbDataT cb = {
      .reason = vhpiCbValueChange,
      .cb_rtn = ab_value_change,
      .obj    = ab,
   };
   VHPI_CHECK(vhpi_register_cb(&cb, 0));

   vhpi_release_handle(ab);
}

static void end_of_sim(const vhpiCbDataT *cb_data)
{
   vhpi_printf("%d value changes", changes);
   fail_unless(changes > 0);
}

void vhpi32_startup(void)
{
   vhpiCbDataT cb_data = {
      .reason = vhpiCbStartOfSimulation,
      .cb_rtn = start_of_sim,
   };
   VHPI_CHECK(vhpi_register_cb(&cb_data, 0));

   vhpiCbDataT end_cb = {
      .reason = vhpiCbEndOfSimulation,
      .cb_rtn = end_of_sim,
   };
   VHPI_CHECK(vhpi_register_cb(&end_cb, 0));
}
