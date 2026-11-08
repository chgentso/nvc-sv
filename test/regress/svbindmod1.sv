// bind declared inside a module, targeting an interface instantiated in that module (self-checking).
// Expected values: IEEE 1800-2017 23.11 (a bind directive may appear in a module; the bound instance is added
// to the target and sees its ports), 21.2.1.6 (%m: hierarchical name), 9.4.2 (@posedge).
// Reference model: clk rises at 5,15,25,35 before $finish at 42; d is 1 only from 12 to 22, i.e. at the edge
// 15: the bound checker sees 4 edges, and exactly one with d set (at t=15).
`timescale 1ns/1ns
interface chk(input bit clk, input bit d);
  int edges = 0, hits = 0;
  longint when_hit = -1;
  string path;
  initial path = $sformatf("%m");
  always @(posedge clk) begin edges++; if (d) begin hits++; when_hit = $time; end end
endinterface
interface mon(input bit clk, input bit d);
endinterface
module agent(input bit clk, input bit d);
  mon m(clk, d);
  bind mon chk c(.clk(clk), .d(d));
endmodule
module svbindmod1;
  bit clk = 0, d = 0;
  int errs = 0;
  always #5 clk = !clk;
  agent a(clk, d);
  initial begin
    #12 d = 1; #10 d = 0; #20;
    if (a.m.c.edges != 4) begin errs++; $error("edges = %0d, expected 4", a.m.c.edges); end
    if (a.m.c.hits != 1 || a.m.c.when_hit != 15) begin errs++; $error("hits = %0d at %0d, expected 1 at 15", a.m.c.hits, a.m.c.when_hit); end
    if (a.m.c.path != "svbindmod1.a.m.c") begin errs++; $error("%%m = %s", a.m.c.path); end
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
