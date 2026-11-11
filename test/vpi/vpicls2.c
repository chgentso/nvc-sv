#include "vpicls_util.h"

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

// Containers and other kinds of property of regress/vpicls2.sv

static void check_elems(vpiHandle arr, int count, const int *values)
{
   vpiHandle it = vpi_iterate(vpiVarSelect, arr);
   if (count == 0) {
      fail_unless(it == NULL);
      return;
   }

   fail_if(it == NULL);
   vpiHandle e;
   int n = 0;
   while ((e = vpi_scan(it)) != NULL) {
      fail_unless(n < count);
      check_int(vpicls_int(e), values[n]);
      n++;
      vpi_release_handle(e);
   }
   check_int(n, count);
}

static vpiHandle elem(vpiHandle arr, int index)
{
   vpiHandle e = vpi_handle_by_index(arr, index);
   check_error();
   return check_handle(e);
}

static void no_elem(vpiHandle arr, int index)
{
   s_vpi_error_info info;
   fail_unless(vpi_handle_by_index(arr, index) == NULL);
   fail_unless(vpi_chk_error(&info));   // Out of range
}

static PLI_INT32 at_3ns(p_cb_data cb)
{
   char names[512];

   vpiHandle top = vpi_test_lookup("vpicls2");
   vpiHandle bx = vpicls_var(top, "bx", 0);
   vpiHandle obj = check_handle(vpi_handle(vpiClassObj, bx));

   check_int(vpicls_names(vpiVariables, obj, names, sizeof(names)), 18);
   check_string(names, "di qi aai aas qh dh ah byobj qs qr qb aal empty "
                "mb mb_null sem rec nest");
   check_int(vpicls_names(vpiNamedEvent, obj, names, sizeof(names)), 1);
   check_string(names, "ev");
   check_int(vpi_get(vpiType, vpicls_var(obj, "di", 0)), vpiArrayVar);

   // Dynamic array of integers
   vpiHandle di = vpicls_var(obj, "di", 0);
   check_int(vpi_get(vpiType, di), vpiRegArray);
   check_int(vpi_get(vpiArray, di), 1);
   check_int(vpi_get(vpiArrayType, di), vpiDynamicArray);
   check_int(vpi_get(vpiSize, di), 3);
   check_string(vpi_get_str(vpiFullName, di), "vpicls2.bx.di");
   const int di_values[] = { 5, 6, 7 };
   check_elems(di, 3, di_values);
   check_int(vpicls_int(elem(di, 2)), 7);
   check_string(vpi_get_str(vpiName, elem(di, 1)), "di[1]");
   check_string(vpi_get_str(vpiFullName, elem(di, 1)), "vpicls2.bx.di[1]");
   no_elem(di, 3);
   no_elem(di, -1);
   check_int(vpicls_int(check_handle(vpi_handle(vpiLeftRange, di))), 0);
   check_int(vpicls_int(check_handle(vpi_handle(vpiRightRange, di))), 2);

   // Queue of integers
   vpiHandle qi = vpicls_var(obj, "qi", 0);
   check_int(vpi_get(vpiArrayType, qi), vpiQueueArray);
   check_int(vpi_get(vpiSize, qi), 4);
   const int qi_values[] = { 11, 22, 33, 44 };
   check_elems(qi, 4, qi_values);

   // An empty one
   vpiHandle empty = vpicls_var(obj, "empty", 0);
   check_int(vpi_get(vpiArrayType, empty), vpiQueueArray);
   check_int(vpi_get(vpiSize, empty), 0);
   check_elems(empty, 0, NULL);
   no_elem(empty, 0);

   // Associative array by integer: elements are in key order
   vpiHandle aai = vpicls_var(obj, "aai", 0);
   check_int(vpi_get(vpiArrayType, aai), vpiAssocArray);
   check_int(vpi_get(vpiSize, aai), 3);
   const int aai_values[] = { 10, 30, 70 };
   check_elems(aai, 3, aai_values);
   check_int(vpicls_int(elem(aai, 3)), 30);
   check_int(vpicls_int(elem(aai, -1)), 10);
   no_elem(aai, 5);
   check_string(vpi_get_str(vpiName, elem(aai, 7)), "aai[7]");
   {
      const int keys[] = { -1, 3, 7 };
      vpiHandle it = vpi_iterate(vpiVarSelect, aai);
      vpiHandle e;
      for (int i = 0; (e = vpi_scan(it)) != NULL; i++) {
         vpiHandle ix = check_handle(vpi_handle(vpiIndex, e));
         check_int(vpicls_int(ix), keys[i]);
         vpi_release_handle(ix);
         vpi_release_handle(e);
      }
   }

   // By string
   vpiHandle aas = vpicls_var(obj, "aas", 0);
   check_int(vpi_get(vpiSize, aas), 2);
   {
      const char *keys[] = { "one", "two" };
      const char *vals[] = { "uno", "dos" };
      const char *enames[] = { "aas[\"one\"]", "aas[\"two\"]" };
      vpiHandle it = vpi_iterate(vpiVarSelect, aas);
      vpiHandle e;
      for (int i = 0; (e = vpi_scan(it)) != NULL; i++) {
         check_int(vpi_get(vpiType, e), vpiStringVar);
         check_string(vpicls_str(e, vpiStringVal), vals[i]);
         check_string(vpi_get_str(vpiName, e), enames[i]);
         vpiHandle ix = check_handle(vpi_handle(vpiIndex, e));
         check_string(vpicls_str(ix, vpiStringVal), keys[i]);
         vpi_release_handle(ix);
         vpi_release_handle(e);
      }
   }

   // Queue of handles with a null in it
   vpiHandle qh = vpicls_var(obj, "qh", 0);
   check_int(vpi_get(vpiSize, qh), 3);
   vpiHandle qh0 = elem(qh, 0), qh1 = elem(qh, 1), qh2 = elem(qh, 2);
   check_int(vpi_get(vpiType, qh0), vpiClassVar);
   check_int(vpicls_oid(qh0), 1);
   check_int(vpicls_oid(qh1), 2);
   check_int(vpicls_oid(qh2), 0);
   fail_unless(vpi_handle(vpiClassObj, qh2) == NULL);
   check_string(vpi_nvc_dynamic_type(qh0), "item");
   vpiHandle qh0_obj = check_handle(vpi_handle(vpiClassObj, qh0));
   check_int(vpicls_int(vpicls_var(qh0_obj, "v", 0)), 10);
   check_string(vpi_get_str(vpiFullName, vpicls_var(qh0_obj, "v", 0)),
                "vpicls2.bx.qh[0].v");

   // Dynamic array of handles
   vpiHandle dh = vpicls_var(obj, "dh", 0);
   check_int(vpi_get(vpiArrayType, dh), vpiDynamicArray);
   check_int(vpicls_oid(elem(dh, 0)), 0);
   check_int(vpicls_oid(elem(dh, 1)), 3);

   // Associative array of handles by string; the objects are those of the
   // queue and of the variable of the process
   vpiHandle ah = vpicls_var(obj, "ah", 0);
   check_int(vpi_get(vpiSize, ah), 2);
   {
      vpiHandle it = vpi_iterate(vpiVarSelect, ah);
      vpiHandle a = vpi_scan(it), c = vpi_scan(it);
      check_int(vpicls_oid(a), 1);
      check_int(vpicls_oid(c), 3);
      check_int(vpi_nvc_object_addr(a), vpi_nvc_object_addr(qh0));
      check_int(vpi_nvc_object_addr(c), vpi_nvc_object_addr(elem(dh, 1)));
      vpi_release_handle(a);
      vpi_release_handle(c);
   }

   // Associative array keyed by object: the key is the object
   vpiHandle byobj = vpicls_var(obj, "byobj", 0);
   check_int(vpi_get(vpiArrayType, byobj), vpiAssocArray);
   check_int(vpi_get(vpiSize, byobj), 2);
   {
      vpiHandle it = vpi_iterate(vpiVarSelect, byobj);
      vpiHandle e;
      int seen = 0;
      while ((e = vpi_scan(it)) != NULL) {
         vpiHandle ix = check_handle(vpi_handle(vpiIndex, e));
         check_int(vpi_get(vpiType, ix), vpiClassObj);
         const int oid = vpicls_oid(ix);
         check_int(vpicls_int(e), oid);   // Value 1 for item 1 and 3 for 3
         fail_unless(oid == 1 || oid == 3);
         seen |= oid;
         char want[32];
         snprintf(want, sizeof(want), "byobj[item#%d]", oid);
         check_string(vpi_get_str(vpiName, e), want);
         vpi_release_handle(ix);
         vpi_release_handle(e);
      }
      check_int(seen, 3);
   }

   // Strings, reals, packed and four-state elements
   vpiHandle qs = vpicls_var(obj, "qs", 0);
   check_int(vpi_get(vpiSize, qs), 2);
   check_string(vpicls_str(elem(qs, 0), vpiStringVal), "x");
   check_string(vpicls_str(elem(qs, 1), vpiStringVal), "yz");
   check_int(vpi_get(vpiSize, elem(qs, 1)), 2);
   vpiHandle qr = vpicls_var(obj, "qr", 0);
   fail_unless(vpicls_real(elem(qr, 0)) == 1.5);
   fail_unless(vpicls_real(elem(qr, 1)) == -2.25);
   vpiHandle qb = vpicls_var(obj, "qb", 0);
   check_int(vpi_get(vpiType, elem(qb, 0)), vpiBitVar);
   check_int(vpi_get(vpiSize, elem(qb, 0)), 8);
   check_int(vpicls_int(elem(qb, 0)), 0xa5);
   check_int(vpicls_int(elem(qb, 1)), 0x0f);
   vpiHandle aal = vpicls_var(obj, "aal", 0);
   check_string(vpicls_str(elem(aal, 2), vpiBinStrVal), "1x0z");

   // Nested containers
   vpiHandle nest = vpicls_var(obj, "nest", 0);
   check_int(vpi_get(vpiSize, nest), 2);
   vpiHandle nest1 = elem(nest, 1);
   check_int(vpi_get(vpiType, nest1), vpiArrayVar);
   check_int(vpi_get(vpiSize, nest1), 2);
   check_int(vpicls_int(elem(nest1, 1)), 201);
   check_int(vpicls_int(elem(elem(nest, 0), 0)), 100);

   // Unpacked structure
   vpiHandle rec = vpicls_var(obj, "rec", 0);
   check_int(vpi_get(vpiType, rec), vpiStructVar);
   check_int(vpicls_names(vpiMember, rec, names, sizeof(names)), 2);
   check_string(names, "a s");
   vpiHandle rit = vpi_iterate(vpiMember, rec);
   check_int(vpicls_int(vpi_scan(rit)), 9);
   check_string(vpicls_str(vpi_scan(rit), vpiStringVal), "rs");

   // Events, mailboxes and semaphores
   vpiHandle evit = vpi_iterate(vpiNamedEvent, obj);
   fail_if(evit == NULL);
   vpiHandle ev = vpi_scan(evit);
   check_int(vpi_get(vpiType, ev), vpiNamedEvent);
   check_string(vpi_get_str(vpiName, ev), "ev");
   vpi_release_handle(evit);

   vpiHandle mb = vpicls_var(obj, "mb", 0);
   check_int(vpi_get(vpiType, mb), vpiClassVar);
   check_int(vpi_get(vpiClassType, mb), vpiMailboxClass);
   check_int(vpi_get(vpiSize, mb), 3);
   fail_unless(vpicls_oid(mb) != 0);
   check_string(vpi_nvc_dynamic_type(mb), "mailbox");
   vpiHandle mbt = check_handle(vpi_handle(vpiClassTypespec, mb));
   check_string(vpi_get_str(vpiName, mbt), "mailbox");
   vpiHandle mn = vpicls_var(obj, "mb_null", 0);
   check_int(vpicls_oid(mn), 0);
   check_int(vpi_get(vpiSize, mn), 0);
   fail_unless(vpi_nvc_dynamic_type(mn) == NULL);
   vpiHandle sem = vpicls_var(obj, "sem", 0);
   check_int(vpi_get(vpiClassType, sem), vpiSemaphoreClass);
   fail_unless(vpicls_oid(sem) != 0);
   check_string(vpi_nvc_dynamic_type(sem), "semaphore");

   // The bulk call has all of the properties, the events included
   vpicls_members_t ms;
   check_int(vpicls_members(bx, &ms), 19);
   const struct { const char *name, *type; int kind, size; } want[] = {
      { "di", "int[]", vpiNvcMemDynArray, 3 },
      { "qi", "int[$]", vpiNvcMemQueue, 4 },
      { "aai", "int[int]", vpiNvcMemAssoc, 3 },
      { "aas", "string[string]", vpiNvcMemAssoc, 2 },
      { "qh", "item[$]", vpiNvcMemQueue, 3 },
      { "dh", "item[]", vpiNvcMemDynArray, 2 },
      { "ah", "item[string]", vpiNvcMemAssoc, 2 },
      { "byobj", "int[item]", vpiNvcMemAssoc, 2 },
      { "qs", "string[$]", vpiNvcMemQueue, 2 },
      { "qr", "real[$]", vpiNvcMemQueue, 2 },
      { "qb", "bit[7:0][$]", vpiNvcMemQueue, 2 },
      { "aal", "logic[3:0][int]", vpiNvcMemAssoc, 1 },
      { "empty", "int[$]", vpiNvcMemQueue, 0 },
      { "ev", "event", vpiNvcMemEvent, -1 },
      { "mb", "mailbox", vpiNvcMemMailbox, 3 },
      { "mb_null", "mailbox", vpiNvcMemMailbox, 0 },
      { "sem", "semaphore", vpiNvcMemSemaphore, -1 },
      { "rec", "rec_t", vpiNvcMemStruct, -1 },
      { "nest", "int[][]", vpiNvcMemDynArray, 2 },
   };
   for (int i = 0; i < ARRAY_LEN(want); i++) {
      check_string(ms.item[i].name, want[i].name);
      check_string(ms.item[i].type_name, want[i].type);
      check_string(ms.item[i].owner, "box");
      check_int(ms.item[i].kind, want[i].kind);
      check_int(ms.item[i].size, want[i].size);
      if (want[i].kind == vpiNvcMemEvent)
         fail_unless(ms.item[i].handle == NULL);
      else
         fail_if(ms.item[i].handle == NULL);
   }

   // A handle from the bulk call is the container
   check_int(vpi_get(vpiSize, ms.item[1].handle), 4);
   check_int(vpicls_int(elem(ms.item[1].handle, 3)), 44);
   vpicls_release_members(&ms);

   // Variables of the module
   const int mq_values[] = { 1, 2, 3 };
   vpiHandle mq = vpicls_var(top, "mq", 0);
   check_int(vpi_get(vpiArrayType, mq), vpiQueueArray);
   check_elems(mq, 3, mq_values);
   const int mda_values[] = { -4, 4 };
   vpiHandle mda = vpicls_var(top, "mda", 0);
   check_int(vpi_get(vpiArrayType, mda), vpiDynamicArray);
   check_elems(mda, 2, mda_values);
   vpiHandle maa = vpicls_var(top, "maa", 0);
   check_int(vpi_get(vpiArrayType, maa), vpiAssocArray);
   vpiHandle mit = vpi_iterate(vpiVarSelect, maa);
   check_string(vpicls_str(vpi_scan(mit), vpiStringVal), "v");
   vpi_release_handle(mit);
   vpiHandle mqh = vpicls_var(top, "mqh", 0);
   check_int(vpicls_oid(elem(mqh, 0)), 3);
   check_int(vpicls_names(vpiVariables, top, names, sizeof(names)), 5);
   check_string(names, "bx mq mda maa mqh");

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

void vpicls2_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
}
