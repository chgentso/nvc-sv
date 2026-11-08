`timescale 1ns/1ns
// Module and interface declarations nested in a module
// Expected values: IEEE 1800-2017 23.4 (nested modules: visible only inside the enclosing module, instantiated like
// any module), 9.4.2 (posedge), 21.2.1.6 (%m is the hierarchical name of the instance)
module svnestmod1;
  bit clk = 0, d = 0;
  always #5 clk = !clk;

  module counter (input bit c, output int n);
    initial n = 0;
    always @(posedge c) n++;
  endmodule

  interface mon (input bit c, input bit v);
    int hits[$];
    string nm;
    initial nm = $sformatf("%m");
    always @(posedge c) if (v) hits.push_back($time);
  endinterface

  int cnt;
  int fails = 0;
  counter u (.c(clk), .n(cnt));
  
  mon m (clk, d);
  initial begin
    #12 d = 1; #10 d = 0; #20;
    // posedges of clk at 5, 15, 25, 35; d is 1 only at the posedge of t=15
    if (cnt != 4) begin $error("cnt=%0d", cnt); fails++; end
    if (m.hits.size() != 1 || m.hits[0] != 15) begin $error("hits: %p", m.hits); fails++; end
    if (m.nm != "svnestmod1.m") begin $error("%%m = %s", m.nm); fails++; end
    if (fails == 0) $display("PASSED");
    $finish;
  end
endmodule
