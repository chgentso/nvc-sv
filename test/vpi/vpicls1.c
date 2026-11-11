#include "vpicls_util.h"

// Class variables, objects, dynamic classes and properties of
// regress/vpicls1.sv

static PLI_INT32 end_of_sim(p_cb_data cb);

static PLI_INT32 at_3ns(p_cb_data cb)
{
   char names[256];

   vpiHandle top = vpi_test_lookup("vpicls1");
   vpiHandle sh = vpicls_var(top, "sh", 0);
   vpiHandle other = vpicls_var(top, "other", 0);
   vpiHandle nullh = vpicls_var(top, "nullh", 0);
   vpiHandle ci = vpicls_var(top, "ci", 0);

   // Class variables are variables of the instance
   check_int(vpi_get(vpiType, sh), vpiClassVar);
   check_string(vpi_get_str(vpiType, sh), "vpiClassVar");
   check_string(vpi_get_str(vpiFullName, sh), "vpicls1.sh");
   check_int(vpicls_names(vpiVariables, top, names, sizeof(names)), 7);
   check_string(names, "sh other nullh ci pr ps pi");

   // A variable which is null has no object
   check_int(vpicls_oid(nullh), 0);
   fail_unless(vpi_handle(vpiClassObj, nullh) == NULL);
   check_error();
   fail_unless(vpi_nvc_dynamic_type(nullh) == NULL);

   // One object seen through three handles has one number
   check_int(vpicls_oid(sh), 1);
   check_int(vpicls_oid(other), 1);
   check_int(vpicls_oid(ci), 1);
   check_int(vpi_get(vpiObjId, sh), 1);
   fail_unless(vpi_nvc_object_addr(sh) != 0);
   check_int(vpi_nvc_object_addr(sh), vpi_nvc_object_addr(other));
   check_int(vpi_nvc_object_addr(sh), vpi_nvc_object_addr(ci));

   // The declared class of the variable and the dynamic class of its
   // object, which is a circle held in a shape variable
   vpiHandle decl = check_handle(vpi_handle(vpiClassTypespec, sh));
   check_int(vpi_get(vpiType, decl), vpiClassTypespec);
   check_string(vpi_get_str(vpiName, decl), "shape");
   check_string(vpi_get_str(vpiFullName, decl), "vpicls1_pkg::shape");
   fail_unless(vpi_handle(vpiExtends, decl) == NULL);

   vpiHandle obj = check_handle(vpi_handle(vpiClassObj, sh));
   check_int(vpi_get(vpiType, obj), vpiClassObj);
   check_int(vpicls_oid(obj), 1);
   check_string(vpi_nvc_dynamic_type(sh), "circle");
   check_string(vpi_nvc_dynamic_type(obj), "circle");
   check_string(vpi_get_str(vpiName, obj), "circle#1");

   vpiHandle dyn = check_handle(vpi_handle(vpiClassTypespec, obj));
   check_string(vpi_get_str(vpiName, dyn), "circle");
   check_string(vpi_get_str(vpiFullName, dyn), "vpicls1_pkg::circle");
   vpiHandle base = check_handle(vpi_handle(vpiExtends, dyn));
   check_string(vpi_get_str(vpiName, base), "shape");
   fail_unless(vpi_handle(vpiExtends, base) == NULL);

   // The properties are those of the base class followed by those of the
   // derived class, and a redeclared one is another property
   check_int(vpicls_names(vpiVariables, obj, names, sizeof(names)), 9);
   check_string(names, "id name col lg wide area name radius peer");

   vpiHandle id = vpicls_var(obj, "id", 0);
   check_int(vpi_get(vpiType, id), vpiIntVar);
   check_int(vpicls_int(id), 1);
   check_int(vpi_get(vpiSize, id), 32);
   check_string(vpi_get_str(vpiFullName, id), "vpicls1.sh.id");

   vpiHandle bname = vpicls_var(obj, "name", 0);
   vpiHandle dname = vpicls_var(obj, "name", 1);
   check_int(vpi_get(vpiType, bname), vpiStringVar);
   check_string(vpicls_str(bname, vpiStringVal), "c0");
   check_string(vpicls_str(dname, vpiStringVal), "c0!");
   check_int(vpi_get(vpiSize, bname), 2);
   check_int(vpi_get(vpiSize, dname), 3);

   vpiHandle col = vpicls_var(obj, "col", 0);
   check_int(vpi_get(vpiType, col), vpiEnumVar);
   check_int(vpicls_int(col), 1);

   // Four-state and wide values
   vpiHandle lg = vpicls_var(obj, "lg", 0);
   check_int(vpi_get(vpiType, lg), vpiReg);
   check_int(vpi_get(vpiSize, lg), 8);
   check_string(vpicls_str(lg, vpiBinStrVal), "1x0z1010");
   vpiHandle wide = vpicls_var(obj, "wide", 0);
   check_int(vpi_get(vpiType, wide), vpiBitVar);
   check_int(vpi_get(vpiSize, wide), 100);
   check_string(vpicls_str(wide, vpiHexStrVal), "0000123456789abcdef012345");

   check_int(vpicls_real(vpicls_var(obj, "area", 0)), 19);
   fail_unless(vpicls_real(vpicls_var(obj, "area", 0)) == 19.5);
   fail_unless(vpicls_real(vpicls_var(obj, "radius", 0)) == 2.5);

   // A property which refers to the object itself
   vpiHandle peer = vpicls_var(obj, "peer", 0);
   check_int(vpi_get(vpiType, peer), vpiClassVar);
   check_int(vpicls_oid(peer), 1);
   check_string(vpi_nvc_dynamic_type(peer), "circle");
   check_int(vpi_nvc_object_addr(peer), vpi_nvc_object_addr(sh));
   vpiHandle peer_obj = check_handle(vpi_handle(vpiClassObj, peer));
   check_int(vpicls_oid(peer_obj), 1);
   vpiHandle peer_decl = check_handle(vpi_handle(vpiClassTypespec, peer));
   check_string(vpi_get_str(vpiName, peer_decl), "shape");
   check_string(vpi_get_str(vpiFullName, peer), "vpicls1.sh.peer");
   vpiHandle peer_id = vpicls_var(peer_obj, "id", 0);
   check_int(vpicls_int(peer_id), 1);
   check_string(vpi_get_str(vpiFullName, peer_id), "vpicls1.sh.peer.id");

   // Static properties belong to the class typespec: two shapes have been
   // made (the circle and the one in the package)
   vpiHandle count = vpicls_var(decl, "count", 0);
   check_int(vpi_get(vpiType, count), vpiIntVar);
   check_int(vpicls_int(count), 2);
   check_int(vpicls_names(vpiVariables, decl, names, sizeof(names)), 1);
   check_string(names, "count");
   check_int(vpicls_names(vpiVariables, dyn, names, sizeof(names)), 1);

   // The bulk call reports all of them, the static ones last
   vpicls_members_t ms;
   check_int(vpicls_members(sh, &ms), 10);
   check_int(ms.n, 10);
   check_string(ms.item[0].name, "id");
   check_string(ms.item[0].type_name, "int");
   check_string(ms.item[0].owner, "shape");
   check_int(ms.item[0].kind, vpiNvcMemScalar);
   check_int(ms.item[0].size, 32);
   check_int(ms.item[0].ival, 1);
   check_int(ms.item[1].kind, vpiNvcMemString);
   check_string(ms.item[1].sval, "c0");
   check_int(ms.item[1].size, 2);
   check_string(ms.item[2].type_name, "color_t");
   check_int(ms.item[2].ival, 1);
   check_string(ms.item[3].type_name, "logic[7:0]");
   check_int(ms.item[3].unknown, 1);
   check_int(ms.item[4].size, 100);
   check_int(ms.item[5].kind, vpiNvcMemReal);
   fail_unless(ms.item[5].rval == 19.5);
   check_string(ms.item[6].name, "name");
   check_string(ms.item[6].owner, "circle");
   check_string(ms.item[6].sval, "c0!");
   check_int(ms.item[8].kind, vpiNvcMemHandle);
   check_string(ms.item[8].name, "peer");
   check_string(ms.item[8].type_name, "shape");
   check_int(ms.item[8].ival, 1);
   check_int(ms.item[8].addr, vpi_nvc_object_addr(sh));
   fail_if(ms.item[8].handle == NULL);
   check_string(ms.item[9].name, "count");
   check_int(ms.item[9].flags, vpiNvcMemStatic);
   check_int(ms.item[9].ival, 2);
   vpicls_release_members(&ms);

   // A null variable has no members
   check_int(vpi_nvc_object_members(nullh, vpicls_collect, &ms), -1);

   // The variable of a package and of a process
   vpiHandle pkg = vpi_test_lookup("vpicls1_pkg");
   vpiHandle pk = vpicls_var(pkg, "pkg_shape", 0);
   check_int(vpi_get(vpiType, pk), vpiClassVar);
   check_int(vpicls_oid(pk), 2);
   check_string(vpi_nvc_dynamic_type(pk), "shape");
   vpiHandle pk_obj = check_handle(vpi_handle(vpiClassObj, pk));
   check_int(vpicls_names(vpiVariables, pk_obj, names, sizeof(names)), 6);
   check_string(names, "id name col lg wide area");
   check_string(vpicls_str(vpicls_var(pk_obj, "name", 0), vpiStringVal), "pk");
   vpiHandle pn = vpicls_var(pkg, "pkg_null", 0);
   check_int(vpicls_oid(pn), 0);
   fail_unless(vpi_handle(vpiClassObj, pn) == NULL);

   vpiHandle proc = NULL;
   vpiHandle pit = vpi_iterate(vpiProcess, top);
   fail_if(pit == NULL);
   while ((proc = vpi_scan(pit)) != NULL) {
      if (vpi_iterate(vpiVariables, proc) != NULL)
         break;
   }
   fail_if(proc == NULL);
   vpiHandle lc = vpicls_var(proc, "local_c", 0);
   check_int(vpi_get(vpiType, lc), vpiClassVar);
   check_int(vpicls_oid(lc), 1);
   check_string(vpi_nvc_dynamic_type(lc), "circle");

   // Specialisations of a class are classes of their own with their own
   // static properties
   vpiHandle pr = vpicls_var(top, "pr", 0);
   vpiHandle ps = vpicls_var(top, "ps", 0);
   vpiHandle pi = vpicls_var(top, "pi", 0);
   check_string(vpi_nvc_dynamic_type(pr), "pcls#(real,8)");
   check_string(vpi_nvc_dynamic_type(ps), "pcls#(string,4)");
   check_string(vpi_nvc_dynamic_type(pi), "pcls#(int,4)");
   check_int(vpicls_oid(pr), 3);
   check_int(vpicls_oid(ps), 4);
   check_int(vpicls_oid(pi), 5);

   vpiHandle pr_obj = check_handle(vpi_handle(vpiClassObj, pr));
   vpiHandle ps_obj = check_handle(vpi_handle(vpiClassObj, ps));
   vpiHandle pi_obj = check_handle(vpi_handle(vpiClassObj, pi));
   vpiHandle prv = vpicls_var(pr_obj, "v", 0);
   check_int(vpi_get(vpiType, prv), vpiRealVar);
   fail_unless(vpicls_real(prv) == 2.5);
   vpiHandle psv = vpicls_var(ps_obj, "v", 0);
   check_int(vpi_get(vpiType, psv), vpiStringVar);
   check_string(vpicls_str(psv, vpiStringVal), "str");
   vpiHandle piv = vpicls_var(pi_obj, "v", 0);
   check_int(vpi_get(vpiType, piv), vpiIntVar);
   check_int(vpicls_int(piv), 42);
   check_int(vpi_get(vpiSize, vpicls_var(pr_obj, "bits", 0)), 8);
   check_int(vpi_get(vpiSize, vpicls_var(ps_obj, "bits", 0)), 4);
   check_string(vpicls_str(vpicls_var(pi_obj, "bits", 0), vpiBinStrVal), "1001");

   vpiHandle prt = check_handle(vpi_handle(vpiClassTypespec, pr));
   check_string(vpi_get_str(vpiName, prt), "pcls#(real,8)");
   check_int(vpicls_int(vpicls_var(prt, "made", 0)), 1);
   check_int(vpicls_int(vpicls_var(
                           check_handle(vpi_handle(vpiClassTypespec, ps)),
                           "made", 0)), 1);

   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
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
   return 0;
}

void vpicls1_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
}
