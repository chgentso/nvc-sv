#include "vpicls_util.h"

// Class handles of a method frame in regress/vpicls6.sv

static struct {
   char      name[16], text[32], cls[32];
   int       kind, is_param;
   PLI_INT64 addr, oid;
} vars[8];
static int nvars, hit_line;

static void on_var(const vpi_nvc_var_t *v, void *user_data)
{
   fail_unless(nvars < 8);
   snprintf(vars[nvars].name, sizeof(vars[0].name), "%s", v->name);
   snprintf(vars[nvars].text, sizeof(vars[0].text), "%s", v->text);
   snprintf(vars[nvars].cls, sizeof(vars[0].cls), "%s", v->class_name ?: "");
   vars[nvars].kind = v->kind;
   vars[nvars].is_param = v->is_param;
   vars[nvars].addr = v->addr;
   vars[nvars].oid = v->oid;
   nvars++;
}

static void on_stmt(const char *file, int line, const char *process,
                    const char *scope, void *user_data)
{
   if (nvars > 0)
      return;

   hit_line = line;
   check_int(vpi_nvc_frame_variables(0, on_var, NULL), 4);

   // The object behind the handle can be read from the callback
   vpiHandle o = check_handle(vpi_nvc_object_at(vars[0].addr, vars[0].oid));
   check_string(vpi_nvc_dynamic_type(o), "derived");
   check_int(vpicls_int(vpicls_var(o, "v", 0)), 7);
}

static const int find(const char *name)
{
   for (int i = 0; i < nvars; i++) {
      if (strcmp(vars[i].name, name) == 0)
         return i;
   }
   vpi_test_fail(__FILE__, __LINE__, "no variable %s", name);
   return 0;
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   vpi_nvc_set_stmt_cb(on_stmt, NULL);
   check_int(vpi_nvc_stmt_breakpoint_add("vpicls6.sv", 26), 1);
   return 0;
}

static PLI_INT32 end_of_sim(p_cb_data cb)
{
   vpi_nvc_set_stmt_cb(NULL, NULL);

   check_int(hit_line, 26);
   check_int(nvars, 4);

   // The parameter holds the derived object, though it is declared item
   const int it = find("it");
   check_int(vars[it].kind, vpiNvcVarObject);
   check_int(vars[it].is_param, 1);
   check_string(vars[it].cls, "derived");
   check_string(vars[it].text, "derived#2");
   check_int(vars[it].oid, 2);
   fail_unless(vars[it].addr != 0);

   // The local holds the same object
   const int loc = find("loc");
   check_int(vars[loc].kind, vpiNvcVarObject);
   check_int(vars[loc].is_param, 0);
   check_int(vars[loc].oid, 2);
   check_int(vars[loc].addr, vars[it].addr);

   // Null handles have no address
   const int none = find("none");
   check_int(vars[none].kind, vpiNvcVarObject);
   check_string(vars[none].text, "null");
   check_int(vars[none].addr, 0);
   check_int(vars[none].oid, 0);
   const int nothing = find("nothing");
   check_int(vars[nothing].kind, vpiNvcVarObject);
   check_string(vars[nothing].text, "null");

   vpi_test_pass();
   return 0;
}

void vpicls6_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
