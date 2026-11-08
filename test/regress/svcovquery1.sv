// Self-checking test of the coverage query methods on a covergroup type, instance, coverpoint and cross.
// Expected values: IEEE 1800-2017 19.8 (get_coverage / get_inst_coverage, static type-level get_coverage),
// 19.11 (item = covered bins / bins; group = average of items; with one instance the type coverage equals
// the instance coverage).
module svcovquery1;
  bit [3:0] v;
  bit w;
  covergroup cg;
    cp_v: coverpoint v { bins lo = {[0:7]}; bins hi = {[8:15]}; }
    cp_w: coverpoint w;
    x_vw: cross cp_v, cp_w;
  endgroup
  cg c = new;
  int fails = 0;
  task automatic chk(string what, real got, real exp);
    if (got < exp - 0.005 || got > exp + 0.005) begin
      fails++; $error("%s: got %0.2f, expected %0.2f", what, got, exp);
    end
  endtask
  initial begin
    real t, i, p, x;
    v = 1; c.sample();   // v=1 hits lo; w=0 hits auto bin 0; cross hits (lo, 0)
    t = cg::get_coverage();
    i = c.get_inst_coverage();
    p = c.cp_v.get_inst_coverage();
    x = c.x_vw.get_coverage();
    chk("cp_v", p, 50.0);                 // 1 of 2 bins
    chk("cp_w", c.cp_w.get_coverage(), 50.0);   // auto bins 0 and 1: 1 of 2
    chk("x_vw", x, 25.0);                 // 1 of 2*2 cross bins
    chk("inst", i, (50.0 + 50.0 + 25.0) / 3);
    chk("type", t, (50.0 + 50.0 + 25.0) / 3);
    if (fails == 0) $display("PASSED");
  end
endmodule
