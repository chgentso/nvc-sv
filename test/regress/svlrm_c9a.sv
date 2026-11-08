// Self-checking: initial/always/always_ff/always_comb/always_latch/final and procedural timing controls.
// Expected values: IEEE 1800-2017 9.2.1 (initial), 9.2.2.2 (always_comb runs once at time 0), 9.2.2.3
// (always_latch sensitivity includes the right-hand side), 9.2.3 (final runs after $finish), 9.4.2 (event control),
// 9.4.3 (wait), 10.4.2 (NBA update before the next time step; samples are taken away from clock edges), 12.4/12.8
// (break). Clock: toggles every 5 ns, posedges at 5,15,25,35 and negedges at 10,20,30,40 (timescale given
// explicitly). Printing of %m is not checked (implementation-defined text).
`timescale 1ns/1ns
`define CHK(g, e) if ((g) !== (e)) begin errs++; $error("check failed at line %0d", `__LINE__); end
module svlrm_c9a;
  int a, b, c;
  int errs;
  logic clk = 0;
  always #5 clk = ~clk;
  always @(posedge clk) a <= a + 1;
  always @(negedge clk) b <= b + 2;
  always_ff @(posedge clk) c <= c + 3;
  logic [3:0] x, y;
  always_comb y = x + 1;
  always_latch if (clk) x = a[3:0];
  int it[3]; int ni;
  int first_pos, a2_time, nt[$];
  initial begin
    #1;
    `CHK(a, 0) `CHK(b, 0) `CHK(c, 0) `CHK(x, 4'bx) `CHK(y, 4'bx)
    #5;                                   // t = 6
    `CHK(a, 1) `CHK(b, 0) `CHK(c, 3) `CHK(x, 4'd1) `CHK(y, 4'd2)
    #5;                                   // t = 11
    `CHK(a, 1) `CHK(b, 2) `CHK(c, 3) `CHK(x, 4'd1) `CHK(y, 4'd2)
    #10;                                  // t = 21
    `CHK(a, 2) `CHK(b, 4) `CHK(c, 6) `CHK(x, 4'd2) `CHK(y, 4'd3)
    #20; $finish;                         // t = 41
  end
  final begin
    if (errs == 0 && $time == 41 && a == 4) $display("PASSED");
    else $display("FAILED: final at %0t a=%0d errs=%0d", $time, a, errs);
  end
  initial begin
    int i;
    for (i = 0; i < 3; i++) begin #1; it[i] = $time; end
    `CHK(it[0], 1) `CHK(it[1], 2) `CHK(it[2], 3)
  end
  initial @(posedge clk) first_pos = $time;
  initial wait (a == 2) a2_time = $time;
  initial forever begin @(negedge clk); if ($time > 30) break; nt.push_back($time); end
  initial begin
    #36; `CHK(first_pos, 5) `CHK(a2_time, 15)
    `CHK(nt.size(), 3) `CHK(nt[0], 10) `CHK(nt[1], 20) `CHK(nt[2], 30)
  end
endmodule
