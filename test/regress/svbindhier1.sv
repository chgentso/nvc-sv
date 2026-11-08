// bind of an interface into an interface instance, connected through a hierarchical name (self-checking).
// Expected values: IEEE 1800-2017 23.11 (bind), 23.6/23.8 (hierarchical and upward names resolve from the
// scope of the bound instance), 21.2.1.6 (%m is the hierarchical name of the instance).
// Reference model: clk rises at 5,15,...,95.  sub.a counts edges (<=), so a == n after the n-th edge.  The
// watcher samples a in the active region of each edge, i.e. the value left by the previous edge, and a 4-bit
// counter equals 3 only at the 4th edge (t=35) within 100 time units (next at the 20th edge, t=195).
`timescale 1ns/1ns
interface sub(input bit clk);
  logic [3:0] a = 0;
  always @(posedge clk) a <= a + 1;
endinterface
interface ifc(input bit clk);
  sub s(clk);
endinterface
interface watch(input bit clk, input logic [3:0] v);
  int hits = 0;
  longint when_hit;
  int val_hit;
  string path;
  initial path = $sformatf("%m");
  always @(posedge clk) if (v == 3) begin hits++; when_hit = $time; val_hit = v; end
endinterface
module svbindhier1;
  bit clk = 0;
  int errs = 0;
  always #5 clk = !clk;
  ifc ii(clk);
  bind ifc watch w(.clk(clk), .v(ii.s.a));
  initial begin
    #100;
    if (ii.w.hits != 1) begin errs++; $error("hits = %0d, expected 1", ii.w.hits); end
    if (ii.w.when_hit != 35) begin errs++; $error("hit at %0d, expected 35", ii.w.when_hit); end
    if (ii.w.val_hit != 3) begin errs++; $error("v = %0d, expected 3", ii.w.val_hit); end
    if (ii.w.path != "svbindhier1.ii.w") begin errs++; $error("%%m = %s", ii.w.path); end
    if (ii.s.a != 4'd10) begin errs++; $error("a = %0d, expected 10", ii.s.a); end
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
