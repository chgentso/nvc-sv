#include "vpi_test.h"

#include <string.h>

static PLI_INT32 at_1ns(p_cb_data cb)
{
   vpiHandle top = vpi_test_lookup("vpi10");

   // Every net can be found again from its name and full name
   int nnets = 0;
   vpiHandle it = vpi_iterate(vpiNet, top), h;
   check_handle(it);
   while ((h = vpi_scan(it))) {
      const char *name = vpi_get_str(vpiName, h);
      fail_if(name == NULL);
      vpiHandle h2 = vpi_handle_by_name((PLI_BYTE8 *)name, top);
      if (h2 == NULL)
         vpi_test_fail(__FILE__, __LINE__, "cannot find '%s'", name);
      else
         fail_unless(vpi_compare_objects(h, h2));

      const char *fullname = vpi_get_str(vpiFullName, h);
      fail_if(fullname == NULL);
      fail_unless(strncmp(fullname, "vpi10.", 6) == 0);
      vpiHandle h3 = vpi_handle_by_name((PLI_BYTE8 *)fullname, NULL);
      if (h3 == NULL)
         vpi_test_fail(__FILE__, __LINE__, "cannot find '%s'", fullname);
      else
         fail_unless(vpi_compare_objects(h, h3));

      nnets++;
   }
   check_int(nnets, 3);

   // The escaped name can be looked up in its escaped form
   vpiHandle weird = vpi_handle_by_name("vpi10.\\weird.sig[1] ", NULL);
   check_handle(weird);
   s_vpi_value val = { .format = vpiScalarVal };
   vpi_get_value(weird, &val);
   check_int(val.value.scalar, vpi1);

   // Named block as an internal scope
   vpiHandle blk = NULL;
   it = vpi_iterate(vpiInternalScope, top);
   check_handle(it);
   while ((h = vpi_scan(it))) {
      if (strcmp(vpi_get_str(vpiName, h), "blk") == 0)
         blk = h;
   }
   check_handle(blk);
   check_int(vpi_get(vpiType, blk), vpiNamedBegin);
   check_string(vpi_get_str(vpiFullName, blk), "vpi10.blk");

   vpiHandle v = vpi_handle_by_name("vpi10.blk.v", NULL);
   check_handle(v);
   check_int(vpi_get(vpiType, v), vpiReg);
   val.format = vpiHexStrVal;
   vpi_get_value(v, &val);
   check_string(val.value.str, "9");

   vpiHandle v2 = vpi_handle_by_name("v", blk);
   check_handle(v2);
   fail_unless(vpi_compare_objects(v, v2));

   // Named event
   vpiHandle ev = vpi_handle_by_name("vpi10.ev", NULL);
   check_handle(ev);
   check_int(vpi_get(vpiType, ev), vpiNamedEvent);

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
   return 0;
}

void vpi10_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
