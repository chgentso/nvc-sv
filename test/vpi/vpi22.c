#include "vpi_test.h"

// What drives the nets and variables of regress/vpi22.sv

static void ignore(const vpi_nvc_conn_t *c, void *user)
{
}

static PLI_INT32 at_2ns(p_cb_data cb);

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   vpi_test_conns_t cs;

   // A net driven by a continuous assignment
   check_int(vpi_test_connections(&cs, "vpi22 n", 0, 4), 1);
   check_int(cs.item[0].c.kind, vpiNvcConnProcess);
   check_string(cs.item[0].kind, "assign");
   check_string(cs.item[0].scope, "vpi22");
   check_int(cs.item[0].c.line, 22);
   check_int(cs.item[0].c.first, 0);
   check_int(cs.item[0].c.count, 4);
   check_int(cs.item[0].c.flags, 0);

   // A net with two drivers
   check_int(vpi_test_connections(&cs, "vpi22 bus", 0, 1), 2);
   fail_if(vpi_test_conn_at(&cs, "vpi22.sv", 23) == NULL);
   fail_if(vpi_test_conn_at(&cs, "vpi22.sv", 24) == NULL);

   // Variables have no driver in the model: the procedures that assign to
   // them are found from their code
   check_int(vpi_test_connections(&cs, "vpi22 r", 0, 4), 1);
   check_string(cs.item[0].kind, "always_comb");
   check_int(cs.item[0].c.line, 26);
   check_int(cs.item[0].c.flags, vpiNvcConnApprox);

   check_int(vpi_test_connections(&cs, "vpi22 s", 0, 4), 1);
   check_string(cs.item[0].kind, "initial");
   check_int(cs.item[0].c.line, 28);

   check_int(vpi_test_connections(&cs, "vpi22 clk", 0, 1), 1);
   check_string(cs.item[0].kind, "always");
   check_int(cs.item[0].c.line, 33);

   // A variable which is the output port of an instance: the instance
   // holds its always_ff
   check_int(vpi_test_connections(&cs, "vpi22 u_cnt q", 0, 4), 1);
   check_string(cs.item[0].kind, "always_ff");
   check_string(cs.item[0].scope, "vpi22 u_cnt");
   check_int(cs.item[0].c.line, 8);

   // ... and the net it is connected to has a connection from that port
   check_int(vpi_test_connections(&cs, "vpi22 q", 0, 4), 1);
   check_int(cs.item[0].c.kind, vpiNvcConnPort);
   check_string(cs.item[0].other, "vpi22 u_cnt q");
   check_string(cs.item[0].scope, "vpi22 u_cnt");
   check_int(cs.item[0].c.flags, vpiNvcConnCollapsed | vpiNvcConnFromChild);

   // An input port shares the storage of the net it is connected to
   check_int(vpi_test_connections(&cs, "vpi22 u_cnt d", 0, 4), 1);
   check_int(cs.item[0].c.kind, vpiNvcConnPort);
   check_string(cs.item[0].other, "vpi22 d");
   check_int(cs.item[0].c.flags, vpiNvcConnCollapsed);
   check_int(vpi_test_connections(&cs, "vpi22 u_cnt clk", 0, 1), 1);
   check_string(cs.item[0].other, "vpi22 clk");

   // Verilog names are case sensitive
   check_int(vpi_nvc_connections("vpi22 N", 0, 0, ignore, NULL), -1);

   vpi_test_cb(cbAfterDelay, at_2ns, 2, NULL);
   return 0;
}

static PLI_INT32 at_2ns(p_cb_data cb)
{
   vpi_test_conns_t cs;

   // A force is one more source of the net
   vpiHandle n = vpi_test_lookup("vpi22.n");
   s_vpi_value val = { .format = vpiIntVal, .value.integer = 9 };
   vpi_put_value(n, &val, NULL, vpiForceFlag);
   check_error();

   check_int(vpi_test_connections(&cs, "vpi22 n", 0, 4), 2);
   int nforce = 0;
   for (int i = 0; i < cs.n; i++) {
      if (cs.item[i].c.kind == vpiNvcConnForce) {
         nforce++;
         check_int(cs.item[i].c.first, 0);
         check_int(cs.item[i].c.count, 4);
      }
   }
   check_int(nforce, 1);

   // The signals of the process that drives n, found by the name it was
   // reported with: it reads q and drives n
   vpi_test_procsigs_t ps;
   check_int(vpi_test_connections(&cs, "vpi22 n", 0, 4), 2);
   for (int i = 0; i < cs.n; i++) {
      if (cs.item[i].c.kind != vpiNvcConnProcess)
         continue;
      check_int(vpi_test_process_signals(&ps, cs.item[i].scope, cs.item[i].name), 2);
      for (int j = 0; j < ps.n; j++)
         check_string(ps.item[j].path, ps.item[j].write ? "vpi22 n" : "vpi22 q");
   }

   vpi_test_pass();
   vpi_control(vpiFinish, 0);
   return 0;
}

void vpi22_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
}
