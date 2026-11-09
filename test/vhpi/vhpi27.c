#include "vhpi_test.h"

#include <string.h>

static void check_location(vhpiHandleT root, const char *name, int line)
{
   vhpiHandleT h = VHPI_CHECK(vhpi_handle_by_name(name, root));

   const char *file = (const char *)vhpi_get_str(vhpiFileNameP, h);
   fail_if(file == NULL);
   const size_t len = strlen(file);
   fail_unless(len >= 10 && strcmp(file + len - 10, "vhpi27.vhd") == 0);
   vhpi_printf("%s at line %d", name, vhpi_get(vhpiLineNoP, h));
   fail_unless(vhpi_get(vhpiLineNoP, h) == line);

   vhpi_release_handle(h);
}

static void start_of_sim(const vhpiCbDataT *cb_data)
{
   vhpiHandleT root = VHPI_CHECK(vhpi_handle(vhpiRootInst, NULL));

   check_location(root, "s1", 5);
   check_location(root, "s2", 6);

   vhpi_release_handle(root);
}

void vhpi27_startup(void)
{
   vhpiCbDataT cb_data = {
      .reason = vhpiCbStartOfSimulation,
      .cb_rtn = start_of_sim,
   };
   VHPI_CHECK(vhpi_register_cb(&cb_data, 0));
}
