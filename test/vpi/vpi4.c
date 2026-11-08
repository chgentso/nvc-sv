#include "vpi_test.h"

static int clk_changes, cnt_changes, rw_count, ro_count, next_count;
static int removed_fired;
static vpiHandle clk_cb, pending_cb;
static PLI_INT32 last_cnt = 0;

static PLI_INT32 read_write(p_cb_data cb)
{
   rw_count++;
   fail_unless(cb->reason == cbReadWriteSynch);
   check_int(vpi_test_now(), 15);

   s_vpi_value val = { .format = vpiIntVal, .value.integer = 0x5a };
   vpi_put_value(vpi_test_lookup("vpi4.rw_target"), &val, NULL, vpiNoDelay);
   check_error();
   return 0;
}

static PLI_INT32 read_only(p_cb_data cb)
{
   ro_count++;
   fail_unless(cb->reason == cbReadOnlySynch);
   check_int(vpi_test_now(), 25);

   // Rising edges at 5, 15 and 25 ns: the nonblocking update of the
   // time step has happened
   s_vpi_value val = { .format = vpiIntVal };
   vpi_get_value(vpi_test_lookup("vpi4.cnt"), &val);
   check_int(val.value.integer, 3);
   return 0;
}

static PLI_INT32 next_sim_time(p_cb_data cb)
{
   next_count++;
   fail_unless(cb->reason == cbNextSimTime);
   // The next time step after 25 ns is the falling edge at 30 ns
   check_int(vpi_test_now(), 30);
   return 0;
}

static PLI_INT32 removed_after_delay(p_cb_data cb)
{
   removed_fired++;
   return 0;
}

static PLI_INT32 clk_change(p_cb_data cb)
{
   clk_changes++;

   fail_unless(cb->reason == cbValueChange);
   fail_unless(cb->obj != NULL);
   fail_unless(cb->time != NULL);
   fail_unless(cb->value != NULL);

   const PLI_UINT64 now = vpi_test_now();
   const PLI_UINT64 t = ((PLI_UINT64)cb->time->high << 32) | cb->time->low;
   check_int(cb->time->type, vpiSimTime);
   check_int(t, now);
   check_int(now, 5 * clk_changes);

   check_int(cb->value->format, vpiScalarVal);
   check_int(cb->value->value.scalar, (clk_changes & 1) ? vpi1 : vpi0);

   switch (now) {
   case 15:
      vpi_test_cb(cbReadWriteSynch, read_write, 0, NULL);
      break;
   case 20:
      // Remove a callback that has not fired yet
      fail_unless(vpi_remove_cb(pending_cb));
      break;
   case 25:
      vpi_test_cb(cbReadOnlySynch, read_only, 0, NULL);
      vpi_test_cb(cbNextSimTime, next_sim_time, 0, NULL);
      break;
   case 30:
      // No more callbacks from now on
      fail_unless(vpi_remove_cb(clk_cb));
      check_error();
      break;
   }

   return 0;
}

static PLI_INT32 cnt_change(p_cb_data cb)
{
   const PLI_UINT64 now = vpi_test_now();
   if (now == 0)
      return 0;    // Initial update of the net by its continuous assignment

   cnt_changes++;
   check_int(now % 10, 5);    // Rising edges only

   check_int(cb->value->format, vpiIntVal);
   check_int(cb->value->value.integer, last_cnt + 1);
   last_cnt = cb->value->value.integer;

   s_vpi_value val = { .format = vpiIntVal };
   vpi_get_value(cb->obj, &val);
   check_int(val.value.integer, last_cnt);
   return 0;
}

static PLI_INT32 end_of_sim(p_cb_data cb)
{
   check_int(vpi_test_now(), 100);
   check_int(clk_changes, 6);       // Removed at 30 ns
   check_int(cnt_changes, 10);      // Rising edges at 5, 15, ..., 95 ns
   check_int(last_cnt, 10);
   check_int(rw_count, 1);
   check_int(ro_count, 1);
   check_int(next_count, 1);
   check_int(removed_fired, 0);

   vpi_test_pass();
   return 0;
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   s_vpi_time t = { .type = vpiSimTime };
   s_vpi_value v = { .format = vpiScalarVal };
   s_cb_data cbd = {
      .reason = cbValueChange,
      .cb_rtn = clk_change,
      .obj    = vpi_test_lookup("vpi4.clk"),
      .time   = &t,
      .value  = &v,
   };
   clk_cb = vpi_register_cb(&cbd);
   check_handle(clk_cb);

   // Value change on a net driven by a variable updated with a
   // nonblocking assignment
   s_vpi_time st = { .type = vpiSuppressTime };
   s_vpi_value iv = { .format = vpiIntVal };
   s_cb_data cbn = {
      .reason = cbValueChange,
      .cb_rtn = cnt_change,
      .obj    = vpi_test_lookup("vpi4.cnt_w"),
      .time   = &st,
      .value  = &iv,
   };
   check_handle(vpi_register_cb(&cbn));

   pending_cb = vpi_test_cb(cbAfterDelay, removed_after_delay, 50, NULL);
   return 0;
}

void vpi4_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
