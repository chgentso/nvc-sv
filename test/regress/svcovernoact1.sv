// cover property / cover sequence with and without pass actions
// Expected values: IEEE 1800-2017 16.14.3 (cover statements; the pass statement runs on each
// successful non-vacuous evaluation), 16.9.2 ($rose, sampled values), 16.7 (##1), 16.12.5
// (disable iff). Clock edges at 5, 15, ...; g is 0, 1 from 12, 0 from 32, 1 from 42, and
// never changes at a clock edge.
//   $rose(g) succeeds at edges 15 and 45; (g ##1 !g) completes at edge 35 only.
`timescale 1ns/1ns
module svcovernoact1;
  logic clk = 0, rst = 0, g = 0;
  int hits = 0;
  int c3hits = 0;
  int c3time = -1;
  always #5 clk = ~clk;
  initial begin
    #12 g = 1; #20 g = 0; #10 g = 1; #40;
    if (hits == 2 && c3hits == 1 && c3time == 35) $display("PASSED");
    else $error("hits=%0d c3hits=%0d c3time=%0d, expected 2 1 35", hits, c3hits, c3time);
    $finish;
  end
  c0: cover property (@(posedge clk) disable iff (rst) $rose(g));
  c1: cover property (@(posedge clk) $rose(g)) hits++;
  c2: cover sequence (@(posedge clk) g ##1 g);
  c3: cover property (@(posedge clk) g ##1 !g) begin c3hits++; c3time = int'($time); end
endmodule
