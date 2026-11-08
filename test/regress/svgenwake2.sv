// Expected values: IEEE 1800-2017 9.4.2 (event control on a 0->1 edge of clk), 23.3 (module instances),
// 27.5 (generate conditional). Same-time process order is unspecified (4.7), so only wakeup counts and times are checked.
`timescale 1ns/1ns
package svgenwake2_pkg;
  int hits[string];
  time when[string];
endpackage
module svgenwake2_sub(input logic clk, input int id);
  always @(posedge clk) begin
    if (!svgenwake2_pkg::hits.exists($sformatf("sub%0d.a", id))) svgenwake2_pkg::hits[$sformatf("sub%0d.a", id)] = 0;
    svgenwake2_pkg::hits[$sformatf("sub%0d.a", id)]++;
    svgenwake2_pkg::when[$sformatf("sub%0d.a", id)] = $time;
  end
endmodule
module svgenwake2;
  import svgenwake2_pkg::*;
  logic clk = 0;
  int err = 0;
  `define HIT(n) always @(posedge clk) begin if (!hits.exists(n)) hits[n] = 0; hits[n]++; when[n] = $time; end
  initial #5 clk = 1;
  `HIT("top-a")
  svgenwake2_sub s1(clk, 1);
  `HIT("top-b")
  if (1) begin : g
    `HIT("g.a")
    svgenwake2_sub s2(clk, 2);
    `HIT("g.b")
  end
  svgenwake2_sub s3(clk, 3);
  `HIT("top-c")
  initial begin
    #10;
    foreach (hits[k]) begin
      if (hits[k] != 1) begin err++; $error("%s: %0d hits", k, hits[k]); end
      if (when[k] != 5) begin err++; $error("%s woke at %0t", k, when[k]); end
    end
    if (hits.num() != 8) begin err++; $error("expected 8 processes, saw %0d", hits.num()); end
    if (err == 0) $display("PASSED");
  end
endmodule
