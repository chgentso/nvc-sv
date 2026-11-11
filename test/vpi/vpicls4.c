#include "vpicls_util.h"

// The tree of components of regress/vpicls4.sv found from the constant of
// a package and from a static property

typedef struct {
   char path[512];
   char dyn[32];
   int  level;
   int  count;
} node_t;

static node_t nodes[8];
static int    nnodes;

static void walk(vpiHandle comp, const char *path, int level)
{
   vpiHandle obj = check_handle(vpi_handle(vpiClassObj, comp));
   vpiHandle level_var = vpicls_var(obj, "level", 0);
   check_int(vpicls_int(level_var), level);

   vpiHandle kids = vpicls_var(obj, "m_children", 0);
   check_int(vpi_get(vpiArrayType, kids), vpiAssocArray);

   // Each child knows its parent by the same object
   vpiHandle it = vpi_iterate(vpiVarSelect, kids);
   vpiHandle kid;
   while (it != NULL && (kid = vpi_scan(it)) != NULL) {
      vpiHandle kobj = check_handle(vpi_handle(vpiClassObj, kid));
      vpiHandle name = vpicls_var(kobj, "m_name", 0);
      vpiHandle parent = vpicls_var(kobj, "m_parent", 0);
      check_int(vpicls_oid(parent), vpicls_oid(comp));
      check_int(vpi_nvc_object_addr(parent), vpi_nvc_object_addr(comp));

      char full[512];
      snprintf(full, sizeof(full), "%s%s%s", path, path[0] ? "." : "",
               vpicls_str(name, vpiStringVal));

      fail_unless(nnodes < 8);
      node_t *n = &(nodes[nnodes++]);
      snprintf(n->path, sizeof(n->path), "%s", full);
      snprintf(n->dyn, sizeof(n->dyn), "%s", vpi_nvc_dynamic_type(kid));
      n->level = level + 1;

      // The name found through the key of the element is the same
      vpiHandle key = check_handle(vpi_handle(vpiIndex, kid));
      check_string(vpicls_str(key, vpiStringVal),
                   vpicls_str(name, vpiStringVal));

      vpiHandle checks = NULL;
      vpiHandle mit = vpi_iterate(vpiVariables, kobj);
      vpiHandle m;
      while ((m = vpi_scan(mit)) != NULL) {
         if (strcmp(vpi_get_str(vpiName, m), "checks") == 0)
            checks = m;
         else
            vpi_release_handle(m);
      }
      n->count = checks != NULL ? vpicls_int(checks) : -1;

      walk(kid, full, level + 1);
      vpi_release_handle(kid);
   }
}

static PLI_INT32 at_3ns(p_cb_data cb)
{
   char names[128];

   vpiHandle pkg = vpi_test_lookup("vpicls4_pkg");
   vpiHandle top = vpicls_var(pkg, "top", 0);
   check_int(vpi_get(vpiType, top), vpiClassVar);
   check_string(vpi_get_str(vpiFullName, top), "vpicls4_pkg::top");
   check_string(vpi_nvc_dynamic_type(top), "root");
   fail_unless(vpicls_oid(top) != 0);

   // The root is the same object as the one in the static property
   vpiHandle decl = check_handle(vpi_handle(vpiClassTypespec, top));
   check_string(vpi_get_str(vpiName, decl), "root");
   vpiHandle inst = vpicls_var(decl, "m_inst", 0);
   check_int(vpi_get(vpiType, inst), vpiClassVar);
   check_int(vpicls_oid(inst), vpicls_oid(top));
   check_int(vpi_nvc_object_addr(inst), vpi_nvc_object_addr(top));
   check_string(vpi_get_str(vpiFullName, inst), "vpicls4_pkg::root::m_inst");

   // Its properties include those of the base class
   vpiHandle obj = check_handle(vpi_handle(vpiClassObj, top));
   check_int(vpicls_names(vpiVariables, obj, names, sizeof(names)), 4);
   check_string(names, "m_name m_parent m_children level");
   check_string(vpicls_str(vpicls_var(obj, "m_name", 0), vpiStringVal), "");
   check_int(vpicls_oid(vpicls_var(obj, "m_parent", 0)), 0);

   walk(top, "", 0);

   check_int(nnodes, 4);
   check_string(nodes[0].path, "env");
   check_string(nodes[0].dyn, "env");
   check_int(nodes[0].level, 1);
   check_int(nodes[0].count, 7);
   check_string(nodes[1].path, "env.agent");
   check_string(nodes[1].dyn, "comp");
   check_int(nodes[1].count, -1);
   // Children are in the order of their names
   check_string(nodes[2].path, "env.agent.drv");
   check_string(nodes[3].path, "env.agent.mon");
   check_int(nodes[3].level, 3);

   // The module's own variables see the same objects
   vpiHandle m = vpi_test_lookup("vpicls4");
   vpiHandle e = vpicls_var(m, "e", 0);
   check_string(vpi_nvc_dynamic_type(e), "env");
   vpiHandle e_decl = check_handle(vpi_handle(vpiClassTypespec, e));
   check_string(vpi_get_str(vpiFullName, e_decl), "vpicls4_pkg::env");
   fail_unless(vpi_iterate(vpiVariables, e_decl) == NULL);   // No statics
   check_error();
   return 0;
}

static PLI_INT32 end_of_sim(p_cb_data cb)
{
   vpi_test_pass();
   return 0;
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   vpi_test_cb(cbAfterDelay, at_3ns, 3, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
   return 0;
}

void vpicls4_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
}
