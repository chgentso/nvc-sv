#include "vhpi_test.h"

#include <string.h>

static vhpiHandleT cnt;
static int changes;

static void cnt_changed(const vhpiCbDataT *cb_data)
{
   vhpiTimeT now;
   vhpi_get_time(&now, NULL);

   vhpiValueT value = { .format = vhpiIntVal };
   VHPI_CHECK(vhpi_get_value(cnt, &value));
   vhpi_printf("cnt = %d at %d", value.value.intg, (int)now.low);

   changes++;
}

static void at_10ns(const vhpiCbDataT *cb_data)
{
   vhpiValueT value = { .format = vhpiIntVal };
   VHPI_CHECK(vhpi_get_value(cnt, &value));
   fail_unless(value.value.intg == 1);

   value.value.intg = 5;
   VHPI_CHECK(vhpi_put_value(cnt, &value, vhpiDepositPropagate));
}

static void end_of_sim(const vhpiCbDataT *cb_data)
{
   // Changes at 5 (1), 10 (5), 15 (6) and 25 (7) ns
   fail_unless(changes == 4);

   vhpiValueT value = { .format = vhpiIntVal };
   VHPI_CHECK(vhpi_get_value(cnt, &value));
   fail_unless(value.value.intg == 7);
}

static void start_of_sim(const vhpiCbDataT *cb_data)
{
   vhpiHandleT root = VHPI_CHECK(vhpi_handle(vhpiRootInst, NULL));

   // The VHDL instance is a statement of the root, which is the
   // Verilog top-level module
   vhpiHandleT inst = NULL, h;
   vhpiHandleT it = VHPI_CHECK(vhpi_iterator(vhpiInternalRegions, root));
   while ((h = vhpi_scan(it))) {
      // The label is a Verilog identifier and keeps its case
      if (strcmp((char *)vhpi_get_str(vhpiNameP, h), "u_cnt") == 0)
         inst = h;
   }
   check_handle(inst);
   fail_unless(vhpi_get(vhpiKindP, inst) == vhpiCompInstStmtK);

   vhpiHandleT du = VHPI_CHECK(vhpi_handle(vhpiDesignUnit, inst));
   fail_unless(vhpi_get(vhpiKindP, du) == vhpiArchBodyK);

   cnt = VHPI_CHECK(vhpi_handle_by_name("vhpi26.u_cnt.cnt", NULL));
   vhpiHandleT cnt2 = VHPI_CHECK(vhpi_handle_by_name("cnt", inst));
   fail_unless(vhpi_compare_handles(cnt, cnt2));

   vhpiCbDataT cb = {
      .reason = vhpiCbValueChange,
      .cb_rtn = cnt_changed,
      .obj    = cnt,
   };
   VHPI_CHECK(vhpi_register_cb(&cb, 0));

   vhpiTimeT delay = { .low = 10000000 };   // 10 ns
   vhpiCbDataT cb2 = {
      .reason = vhpiCbAfterDelay,
      .cb_rtn = at_10ns,
      .time   = &delay,
   };
   VHPI_CHECK(vhpi_register_cb(&cb2, 0));
}

void vhpi26_startup(void)
{
   vhpiCbDataT cb_data = {
      .reason = vhpiCbStartOfSimulation,
      .cb_rtn = start_of_sim,
   };
   VHPI_CHECK(vhpi_register_cb(&cb_data, 0));

   vhpiCbDataT cb_end = {
      .reason = vhpiCbEndOfSimulation,
      .cb_rtn = end_of_sim,
   };
   VHPI_CHECK(vhpi_register_cb(&cb_end, 0));
}
