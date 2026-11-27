#include "vhpi_test.h"

#include <stdio.h>

static void show(const vhpiCbDataT *cb_data)
{
   vhpiHandleT param = VHPI_CHECK(vhpi_handle_by_index(vhpiParamDecls,
                                                         cb_data->obj, 0));
   vhpiValueT value = { .format = vhpiIntVal };
   VHPI_CHECK(vhpi_get_value(param, &value));
   vhpi_release_handle(param);

   vhpi_printf("show x=%d", value.value.intg);
   printf("show printf x=%d\n", value.value.intg);
   fflush(stdout);
}

static void value_change(const vhpiCbDataT *cb_data)
{
   vhpiValueT value = { .format = vhpiIntVal };
   VHPI_CHECK(vhpi_get_value(cb_data->obj, &value));
   vhpi_printf("value change s=%d", value.value.intg);
}

static void start_of_sim(const vhpiCbDataT *cb_data)
{
   vhpiHandleT root = VHPI_CHECK(vhpi_handle(vhpiRootInst, NULL));
   vhpiHandleT s = VHPI_CHECK(vhpi_handle_by_name("s", root));

   static vhpiValueT value = { .format = vhpiIntVal };
   vhpiCbDataT cb = {
      .reason = vhpiCbValueChange,
      .cb_rtn = value_change,
      .obj = s,
      .value = &value,
   };
   VHPI_CHECK(vhpi_register_cb(&cb, 0));
}

void vhpi_rorder1_startup(void)
{
   vhpiForeignDataT data = {
      .kind = vhpiProcF,
      .libraryName = "vhpi_rorder1",
      .modelName = "show",
      .execf = show,
   };
   vhpiHandleT h = VHPI_CHECK(vhpi_register_foreignf(&data));
   vhpi_release_handle(h);

   vhpiCbDataT start = {
      .reason = vhpiCbStartOfSimulation,
      .cb_rtn = start_of_sim,
   };
   VHPI_CHECK(vhpi_register_cb(&start, 0));
}
