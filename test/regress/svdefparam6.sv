// defparam inside a generate loop, propagating through a child module's own parameter override.
// Expected values: IEEE 1800-2017 23.10.1 (defparam), 27.4 (loop generate), 23.10.2 (the child's #(.ID(SLAVE_ID))
// sees the overridden SLAVE_ID), 23.6 (hierarchical references to parameters). Each leaf records the time its
// parameter-dependent delay (ID*3+K+1) expired. Hierarchical-name text (%m) is not checked.
module leaf #(parameter ID = 7, parameter K = 0) ();
  time fired = 0;
  initial begin #(ID * 3 + K + 1); fired = $time; end
endmodule
module bfm #(parameter SLAVE_ID = 0, parameter K = 0) ();
  leaf #(.ID(SLAVE_ID), .K(K)) l();
endmodule
module svdefparam6;
  genvar i;
  localparam int BASE = 10;
  generate
    for (i = 0; i < 3; i++) begin : g
      bfm #(.K(0)) b();
      bfm #(.K(1)) c();
      bfm #(.K(2)) d();
      defparam g[i].b.SLAVE_ID = i;
      defparam c.SLAVE_ID = BASE + i * 2;
      defparam d.SLAVE_ID = i;
    end
  endgenerate
  int errs = 0;
  task automatic chk(string what, int got, int exp);
    if (got !== exp) begin errs++; $error("%s: got %0d, expected %0d", what, got, exp); end
  endtask
  task automatic leafchk(string n, int id, int k, time f, int eid, int ek);
    chk({n, ".ID"}, id, eid); chk({n, ".K"}, k, ek); chk({n, " fired"}, int'(f), eid*3 + ek + 1);
  endtask
  initial begin
    #1000;
    leafchk("g[0].b.l", g[0].b.l.ID, g[0].b.l.K, g[0].b.l.fired, 0, 0);
    leafchk("g[1].b.l", g[1].b.l.ID, g[1].b.l.K, g[1].b.l.fired, 1, 0);
    leafchk("g[2].b.l", g[2].b.l.ID, g[2].b.l.K, g[2].b.l.fired, 2, 0);
    leafchk("g[0].c.l", g[0].c.l.ID, g[0].c.l.K, g[0].c.l.fired, 10, 1);
    leafchk("g[1].c.l", g[1].c.l.ID, g[1].c.l.K, g[1].c.l.fired, 12, 1);
    leafchk("g[2].c.l", g[2].c.l.ID, g[2].c.l.K, g[2].c.l.fired, 14, 1);
    leafchk("g[0].d.l", g[0].d.l.ID, g[0].d.l.K, g[0].d.l.fired, 0, 2);
    leafchk("g[1].d.l", g[1].d.l.ID, g[1].d.l.K, g[1].d.l.fired, 1, 2);
    leafchk("g[2].d.l", g[2].d.l.ID, g[2].d.l.K, g[2].d.l.fired, 2, 2);
    if (errs == 0) $display("PASSED");
  end
endmodule
