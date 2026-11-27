#include "vpi_test.h"

#include <stdio.h>
#include <string.h>

static PLI_INT32 on_start(p_cb_data cb)
{
   for (int i = 4; i <= 7; i++) {
      char path[64];
      snprintf(path, sizeof(path), "vpi_genvar1.gblk[%d]", i);
      vpiHandle scope = vpi_test_lookup(path);
      check_handle(scope);

      // Iterate vpiParameter inside each shared vpiGenScope and check idx
      bool found_idx = false;
      int idx_val = -1;
      vpiHandle it = vpi_iterate(vpiParameter, scope);
      check_handle(it);

      vpiHandle p;
      while ((p = vpi_scan(it)) != NULL) {
         const char *name = vpi_get_str(vpiName, p);
         if (name != NULL && strcmp(name, "idx") == 0) {
            found_idx = true;
            s_vpi_value v = { .format = vpiIntVal };
            vpi_get_value(p, &v);
            check_error();
            idx_val = v.value.integer;
         }
         vpi_release_handle(p);
      }

      fail_unless(found_idx);
      check_int(idx_val, i);

      // Direct handle lookup by name
      snprintf(path, sizeof(path), "vpi_genvar1.gblk[%d].idx", i);
      vpiHandle h = vpi_test_lookup(path);
      check_handle(h);
      s_vpi_value v = { .format = vpiIntVal };
      vpi_get_value(h, &v);
      check_error();
      check_int(v.value.integer, i);
      vpi_release_handle(h);
      vpi_release_handle(scope);
   }

   return 0;
}

static PLI_INT32 on_end(p_cb_data cb)
{
   vpi_test_pass();
   return 0;
}

void vpi_genvar1_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, on_start, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, on_end, 0, NULL);
}
