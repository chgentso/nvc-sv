// Expected values: IEEE 1800-2017 14.3 / 14.4 (clocking block inputs sampled at the clocking event, default input
// skew #1step), 14.16 (synchronous drives: with output skew #0 the drive happens at the clocking event; a cycle delay
// ##2 delays it by two clocking events), 10.4.2 (nonblocking assignment not visible in the same time step), 25.9
// clk rises at 5, 15, 25, ...; s and d are set to 1 at 7.
`timescale 1ns/1ns
package pk;
  class c;
    virtual ifc vif;
    int errs = 0;
    int cnt_after;
    task run();
      @(posedge vif.cb.s);
      if ($time !== 15) begin errs++; $error("posedge s at %0t, expected 15", $time); end
      #1 vif.cb.o <= 1;
      @(vif.cb);
      if ($time !== 25) begin errs++; $error("cb event at %0t, expected 25", $time); end
      // the drive of o and the sampled d: d was 1 at the preponed region of 25
      if (vif.cb.d !== 1'b1) begin errs++; $error("cb.d=%b, expected 1", vif.cb.d); end
      vif.cb.o <= ##2 0;
      @(vif.cb);
      @(vif.cb);
      if ($time !== 45) begin errs++; $error("second wait ended at %0t, expected 45", $time); end
      vif.mp.cnt <= 3;
      cnt_after = vif.mp.cnt;
    endtask
  endclass
endpackage

interface ifc(input bit clk);
  logic s, o, d;
  int cnt;
  clocking cb @(posedge clk);
    input s, d;
    output o;
  endclocking
  modport mp(output cnt);
endinterface

module svvifclockfwd1;
  bit clk;
  ifc i(clk);
  pk::c o = new;
  initial forever #5 clk = ~clk;
  initial begin
    o.vif = i;
    fork o.run(); join_none
    #7 i.s = 1; i.d = 1;
    #60;
    // after the run: o was driven to 1 at the clocking event 25 and back to 0 two clocking events later (45)
    if (o.cnt_after !== 0) begin o.errs++; $error("cnt visible in the same time step: %0d", o.cnt_after); end
    if (i.cnt !== 3) begin o.errs++; $error("cnt=%0d, expected 3", i.cnt); end
    if (i.o !== 1'b0) begin o.errs++; $error("o=%b at 67, expected 0", i.o); end
    if (o.errs == 0) $display("PASSED");
    $finish;
  end
  // o becomes 1 at the clocking event 25 (not before) and stays 1 until 45
  initial begin
    #24;  if (i.o === 1'b1) begin o.errs++; $error("o driven before the clocking event"); end
    #3;   if (i.o !== 1'b1) begin o.errs++; $error("o=%b at 27, expected 1", i.o); end
    #17;  if (i.o !== 1'b1) begin o.errs++; $error("o=%b at 44, expected 1", i.o); end
    #3;   if (i.o !== 1'b0) begin o.errs++; $error("o=%b at 47, expected 0", i.o); end
  end
endmodule
