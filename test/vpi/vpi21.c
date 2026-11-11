#include "vpi_test.h"

#include <string.h>

// What drives the signals of regress/vpi21.vhd and vpi21.v, from the model
// of the simulation (vpi_nvc_connections)

static void ignore(const vpi_nvc_conn_t *c, void *user)
{
}

static PLI_INT32 end_of_step(p_cb_data cb)
{
   // Every process has run once and waits
   // The signals of a process: what it drives and what it waits for
   vpi_test_procsigs_t ps;
   check_int(vpi_test_process_signals(&ps, "vpi21", "p_lo"), 2);
   for (int i = 0; i < ps.n; i++) {
      if (ps.item[i].write) {
         check_string(ps.item[i].path, "vpi21 q");
         check_int(ps.item[i].first, 2);
         check_int(ps.item[i].count, 2);
         check_int(ps.item[i].width, 4);
      }
      else {
         check_string(ps.item[i].path, "vpi21 clk");
         check_int(ps.item[i].count, 1);
      }
   }
   // a process in an instance, found by the path of the scope; a collapsed
   // port is the signal in the scope above
   check_int(vpi_test_process_signals(&ps, "vpi21 u_leaf", "p_out"), 2);
   for (int i = 0; i < ps.n; i++) {
      if (ps.item[i].write)
         check_string(ps.item[i].path, "vpi21 u_leaf b");
      else
         check_string(ps.item[i].path, "vpi21 u_leaf tmp");
   }
   check_int(vpi_nvc_process_signals("vpi21", "nosuch", NULL, NULL), -1);
   check_int(vpi_nvc_process_signals("nosuch", "p_lo", NULL, NULL), -1);

   vpi_test_pass();
   vpi_control(vpiFinish, 0);
   return 0;
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   vpi_test_conns_t cs;
   const vpi_test_conn_t *c;

   // A vector driven in two parts by different processes; the elements of a
   // vector count from its leftmost bit
   check_int(vpi_test_connections(&cs, "vpi21 q", 0, 4), 2);
   c = vpi_test_conn_at(&cs, "vpi21.vhd", 49);
   fail_if(c == NULL);
   check_int(c->c.kind, vpiNvcConnProcess);
   check_string(c->name, "p_lo");
   check_string(c->kind, "process");
   check_string(c->scope, "vpi21");
   check_int(c->c.first, 2);
   check_int(c->c.count, 2);
   check_int(c->c.total, 4);
   check_int(c->c.flags, 0);
   c = vpi_test_conn_at(&cs, "vpi21.vhd", 54);   // The concurrent assignment
   fail_if(c == NULL);
   check_int(c->c.kind, vpiNvcConnProcess);
   check_int(c->c.first, 0);
   check_int(c->c.count, 2);

   // A part of it: only the process that drives that part, and the
   // range is relative to the start of the part
   check_int(vpi_test_connections(&cs, "vpi21 q", 2, 2), 1);
   check_string(cs.item[0].name, "p_lo");
   check_int(cs.item[0].c.first, 0);
   check_int(cs.item[0].c.count, 2);
   check_int(cs.item[0].c.total, 2);

   // Names are not case sensitive for VHDL
   check_int(vpi_test_connections(&cs, "VPI21 Q", 0, 4), 2);

   // A resolved signal has two drivers
   check_int(vpi_test_connections(&cs, "vpi21 bus1", 0, 1), 2);
   fail_if(vpi_test_conn_at(&cs, "vpi21.vhd", 56) == NULL);
   fail_if(vpi_test_conn_at(&cs, "vpi21.vhd", 62) == NULL);
   check_string(vpi_test_conn_at(&cs, "vpi21.vhd", 56)->name, "p_a");

   // The signal in the instance of a VHDL entity is driven by a process
   check_int(vpi_test_connections(&cs, "vpi21 u_leaf b", 0, 2), 1);
   check_string(cs.item[0].name, "p_out");
   check_string(cs.item[0].scope, "vpi21 u_leaf");
   check_int(cs.item[0].c.line, 20);
   check_int(vpi_test_connections(&cs, "vpi21 u_leaf tmp", 0, 2), 1);
   check_string(cs.item[0].kind, "process");

   // ... the signal it is connected to is driven by the port (an output
   // port is a connection that the model has)
   check_int(vpi_test_connections(&cs, "vpi21 w", 0, 2), 1);
   check_int(cs.item[0].c.kind, vpiNvcConnPort);
   check_string(cs.item[0].other, "vpi21 u_leaf b");
   check_int(cs.item[0].c.other_first, 0);
   check_string(cs.item[0].scope, "vpi21 u_leaf");
   check_int(cs.item[0].c.flags, vpiNvcConnFromChild);
   fail_if(cs.item[0].c.line == 0);

   // An input port that nvc merges with the signal it is connected to
   // (q(3 downto 2)) has no connection in the model: it is restored
   check_int(vpi_test_connections(&cs, "vpi21 u_leaf a", 0, 2), 1);
   check_int(cs.item[0].c.kind, vpiNvcConnPort);
   check_string(cs.item[0].other, "vpi21 q");
   check_int(cs.item[0].c.other_first, 0);
   check_int(cs.item[0].c.count, 2);
   check_int(cs.item[0].c.flags, vpiNvcConnCollapsed);

   // Verilog: a net driven by a continuous assignment
   check_int(vpi_test_connections(&cs, "vpi21 u_sub t", 0, 4), 1);
   check_int(cs.item[0].c.kind, vpiNvcConnProcess);
   check_string(cs.item[0].kind, "assign");
   check_string(cs.item[0].scope, "vpi21 u_sub");
   check_int(cs.item[0].c.line, 9);
   fail_if(vpi_test_conn_at(&cs, "vpi21.v", 9) == NULL);
   check_int(cs.item[0].c.flags, 0);

   // ... variables have no driver in the model: the processes which
   // assign to them are found from their code (flagged)
   check_int(vpi_test_connections(&cs, "vpi21 u_sub r", 0, 4), 1);
   check_string(cs.item[0].kind, "always");
   check_int(cs.item[0].c.line, 11);
   check_int(cs.item[0].c.flags, vpiNvcConnApprox);
   check_int(vpi_test_connections(&cs, "vpi21 u_sub z", 0, 1), 1);
   check_string(cs.item[0].kind, "initial");
   check_int(cs.item[0].c.line, 12);

   // The VHDL port of a Verilog instance is driven by the process which
   // converts the value of the Verilog output
   check_int(vpi_test_connections(&cs, "vpi21 u_sub q", 0, 4), 1);
   check_string(cs.item[0].kind, "conversion");
   check_int(vpi_test_connections(&cs, "vpi21 vq", 0, 4), 1);
   check_int(cs.item[0].c.kind, vpiNvcConnPort);
   check_string(cs.item[0].other, "vpi21 u_sub q");
   check_int(cs.item[0].c.flags, vpiNvcConnFromChild);

   // The loads, from the static description of the design: the processes
   // that read a signal, whether or not they wait for it, and the ports it
   // is connected to
   check_int(vpi_test_connections(&cs, "vpi21 clk", 0, 1), 1);
   check_int(cs.nloads, 3);
   c = vpi_test_load_at(&cs, "vpi21.vhd", 49);       // p_lo
   fail_if(c == NULL);
   check_string(c->name, "p_lo");
   check_string(c->kind, "process");
   check_string(c->scope, "vpi21");
   fail_unless(c->c.is_load);
   fail_if(vpi_test_load_at(&cs, "vpi21.vhd", 47) == NULL);  // clk <= not clk
   int ports = 0;
   for (int i = 0; i < cs.nloads; i++) {
      if (cs.load[i].c.kind == vpiNvcConnPort) {
         ports++;
         check_string(cs.load[i].other, "vpi21 u_sub clk");
         check_string(cs.load[i].scope, "vpi21 u_sub");
         check_int(cs.load[i].c.flags & vpiNvcConnFromChild,
                   vpiNvcConnFromChild);
      }
   }
   check_int(ports, 1);

   // A vector read through a port, part of it by a process of the instance
   check_int(vpi_test_connections(&cs, "vpi21 q", 0, 4), 2);
   check_int(cs.nloads, 1);
   check_int(cs.load[0].c.kind, vpiNvcConnPort);
   check_string(cs.load[0].other, "vpi21 u_leaf a");
   check_int(cs.load[0].c.first, 0);
   check_int(cs.load[0].c.count, 2);
   check_int(cs.load[0].c.other_width, 2);
   check_int(vpi_test_connections(&cs, "vpi21 u_leaf a", 0, 2), 1);
   check_int(cs.nloads, 1);
   check_string(cs.load[0].kind, "process");
   check_string(cs.load[0].scope, "vpi21 u_leaf");
   check_int(vpi_test_connections(&cs, "vpi21 u_leaf tmp", 0, 2), 1);
   check_int(cs.nloads, 1);
   check_string(cs.load[0].name, "p_out");
   // nothing reads it
   check_int(vpi_test_connections(&cs, "vpi21 w", 0, 2), 1);
   check_int(cs.nloads, 0);

   // Unknown names
   check_int(vpi_nvc_connections("vpi21 nosuch", 0, 0, ignore, NULL), -1);
   check_int(vpi_nvc_connections("vpi21", 0, 0, ignore, NULL), -1);
   check_int(vpi_nvc_connections("vpi21 q", 0, 4, ignore, NULL), 0);

   vpi_test_cb(cbReadWriteSynch, end_of_step, 0, NULL);
   return 0;
}

void vpi21_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
}
