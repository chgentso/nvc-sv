#include "vpicls_util.h"

// Validation and pinning of objects in regress/vpicls3.sv

static PLI_INT64 keep_addr, keep_oid, dead_addr, dead_oid;
static vpiHandle pinned, pinned_next;

static PLI_INT32 at_1ns(p_cb_data cb)
{
   vpiHandle top = vpi_test_lookup("vpicls3");
   vpiHandle keep = vpicls_var(top, "keep", 0);
   vpiHandle dead = vpicls_var(top, "dead", 0);
   vpiHandle pinme = vpicls_var(top, "pinme", 0);

   keep_addr = vpi_nvc_object_addr(keep);
   keep_oid = vpicls_oid(keep);
   dead_addr = vpi_nvc_object_addr(dead);
   dead_oid = vpicls_oid(dead);
   fail_unless(keep_addr != 0);
   fail_unless(dead_addr != 0);
   fail_unless(keep_addr != dead_addr);
   check_int(keep_oid, 1);
   check_int(dead_oid, 2);

   // The address of a live object, with its number or without
   check_int(vpi_nvc_object_valid(keep_addr, keep_oid), 1);
   check_int(vpi_nvc_object_valid(keep_addr, 0), 1);
   check_int(vpi_nvc_object_valid(dead_addr, dead_oid), 1);

   // The wrong number means it is another object or has been reclaimed
   check_int(vpi_nvc_object_valid(keep_addr, keep_oid + 1), 0);
   check_int(vpi_nvc_object_valid(keep_addr, dead_oid), 0);

   // Addresses which are not the start of an object are rejected without
   // being read
   int local = 5;
   static int global = 6;
   check_int(vpi_nvc_object_valid(0, 0), 0);
   check_int(vpi_nvc_object_valid(1, 0), 0);
   check_int(vpi_nvc_object_valid(8, 0), 0);
   check_int(vpi_nvc_object_valid(-1, 0), 0);
   check_int(vpi_nvc_object_valid((PLI_INT64)(intptr_t)&local, 0), 0);
   check_int(vpi_nvc_object_valid((PLI_INT64)(intptr_t)&global, 0), 0);
   check_int(vpi_nvc_object_valid((PLI_INT64)(intptr_t)top, 0), 0);
   check_int(vpi_nvc_object_valid(keep_addr + 1, 0), 0);
   check_int(vpi_nvc_object_valid(keep_addr + 8, 0), 0);
   check_int(vpi_nvc_object_valid(keep_addr + 24, 0), 0);
   check_int(vpi_nvc_object_valid(keep_addr - 8, 0), 0);
   check_int(vpi_nvc_object_valid(keep_addr + ((PLI_INT64)1 << 40), 0), 0);

   fail_unless(vpi_nvc_object_at(0, 0) == NULL);
   fail_unless(vpi_nvc_object_at(keep_addr + 8, 0) == NULL);
   fail_unless(vpi_nvc_object_at(keep_addr, 77) == NULL);
   check_error();

   // An object found by address behaves like the one in the variable
   vpiHandle at = check_handle(vpi_nvc_object_at(keep_addr, keep_oid));
   check_int(vpi_get(vpiType, at), vpiClassObj);
   check_int(vpicls_oid(at), keep_oid);
   check_string(vpi_nvc_dynamic_type(at), "node");
   check_int(vpi_nvc_object_addr(at), keep_addr);
   check_int(vpicls_int(vpicls_var(at, "v", 0)), 1);
   vpiHandle at_next = vpicls_var(at, "next", 0);
   check_int(vpicls_oid(at_next), 0);
   vpiHandle at_ts = check_handle(vpi_handle(vpiClassTypespec, at));
   check_string(vpi_get_str(vpiName, at_ts), "node");

   // Pin an object which is only reachable through the variable which the
   // simulation is about to clear, and the one it refers to
   pinned = check_handle(vpi_nvc_pin(pinme));
   check_int(vpi_get(vpiType, pinned), vpiClassObj);
   check_int(vpicls_oid(pinned), 3);
   check_int(vpicls_int(vpicls_var(pinned, "v", 0)), 3);
   vpiHandle next = vpicls_var(pinned, "next", 0);
   check_int(vpicls_oid(next), 4);
   pinned_next = check_handle(vpi_handle(vpiClassObj, next));
   check_int(vpicls_oid(pinned_next), 4);
   fail_unless(vpi_nvc_pin(NULL) == NULL);
   fail_unless(vpi_nvc_pin(top) == NULL);
   vpi_chk_error(NULL);

   return 0;
}

static PLI_INT32 at_6ns(p_cb_data cb)
{
   vpiHandle top = vpi_test_lookup("vpicls3");
   vpiHandle keep = vpicls_var(top, "keep", 0);
   vpiHandle dead = vpicls_var(top, "dead", 0);
   vpiHandle pinme = vpicls_var(top, "pinme", 0);

   // The variables have been cleared but the one held by a variable lives
   check_int(vpicls_oid(dead), 0);
   check_int(vpicls_oid(pinme), 0);
   check_int(vpicls_oid(keep), keep_oid);
   check_int(vpi_nvc_object_addr(keep), keep_addr);
   check_int(vpi_nvc_object_valid(keep_addr, keep_oid), 1);
   fail_if(vpi_nvc_object_at(keep_addr, keep_oid) == NULL);

   // The object nothing refers to now has been reclaimed and the block is
   // free or holds something else
   check_int(vpi_nvc_object_valid(dead_addr, dead_oid), 0);
   fail_unless(vpi_nvc_object_at(dead_addr, dead_oid) == NULL);
   check_error();

   // The pinned objects are still alive and readable
   check_int(vpicls_oid(pinned), 3);
   check_int(vpicls_int(vpicls_var(pinned, "v", 0)), 3);
   check_int(vpicls_oid(pinned_next), 4);
   check_int(vpicls_int(vpicls_var(pinned_next, "v", 0)), 4);
   const PLI_INT64 pinned_addr = vpi_nvc_object_addr(pinned);
   check_int(vpi_nvc_object_valid(pinned_addr, 3), 1);
   check_int(vpi_nvc_object_valid(vpi_nvc_object_addr(pinned_next), 4), 1);

   vpiHandle again = check_handle(vpi_nvc_object_at(pinned_addr, 3));
   check_int(vpicls_int(vpicls_var(again, "v", 0)), 3);

   // Unpinning makes the object a candidate for collection and the handle
   // refers to nothing
   vpi_nvc_unpin(pinned);
   check_error();
   check_int(vpicls_oid(pinned), 0);
   check_int(vpi_nvc_object_addr(pinned), 0);
   fail_unless(vpi_nvc_dynamic_type(pinned) == NULL);

   // Only a pinned object can be unpinned
   vpi_nvc_unpin(again);
   s_vpi_error_info info;
   fail_unless(vpi_chk_error(&info));

   return 0;
}

static PLI_INT32 end_of_sim(p_cb_data cb)
{
   vpi_test_pass();
   return 0;
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   vpi_test_cb(cbAfterDelay, at_1ns, 1, NULL);
   vpi_test_cb(cbAfterDelay, at_6ns, 6, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
   return 0;
}

void vpicls3_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
}
