// Self-checking. Expected values: IEEE 1800-2017 16.5.1 (assertion expressions use values sampled in the
// Preponed region), 16.9.3 ($rose, $past), 16.12.1/16.14.1 (vacuous success of |-> runs the pass action),
// 16.15 (disable iff uses the current, unsampled value, evaluated after the blocking updates), 16.14.3 (cover).
// Clock edges are at t=5,15,25,... ; bit k of a mask is set by the attempt at t=10*k+5.
`timescale 1ns/1ns
`define CHK(tag, got, exp) if ((got) !== (exp)) begin errs++; $error("%s: got %0d expected %0d", tag, got, exp); end
module svapre1;
  int errs = 0;
  logic clk = 0;
  logic a = 0, b = 0;
  logic [3:0] cnt = 0;
  int pm[1:6], fm[1:6], cv = 0;
  always #5 clk = ~clk;
  always @(posedge clk) a = b;
  always @(posedge clk) b <= ~b;
  always @(posedge clk) cnt = cnt + 1;
  initial begin
    for (int i = 1; i <= 6; i++) begin pm[i] = 0; fm[i] = 0; end
    #62;
    // sampled (pre-edge) values at ticks 0..5: a=0,0,1,0,1,0  b=0,1,0,1,0,1  cnt=0..5
    // p1: a==b only at tick 0
    `CHK("p1 pass", pm[1], 32'b0000001)  `CHK("p1 fail", fm[1], 32'b0111110)
    // p2: complement of p1
    `CHK("p2 pass", pm[2], 32'b0111110)  `CHK("p2 fail", fm[2], 32'b0000001)
    // p3: cnt != 3 fails only at tick 3 (sampled cnt == tick number)
    `CHK("p3 pass", pm[3], 32'b0110111)  `CHK("p3 fail", fm[3], 32'b0001000)
    // p4: $rose(b) at ticks 1,3,5 with a==0 -> fail; ticks 0,2,4 are vacuous successes (pass action runs)
    `CHK("p4 pass", pm[4], 32'b0010101)  `CHK("p4 fail", fm[4], 32'b0101010)
    // p5: disable iff (cnt==2): the current cnt after the edge update is tick+1, so tick 1 is disabled;
    // cnt != 4 fails at tick 4 (sampled cnt == 4)
    `CHK("p5 pass", pm[5], 32'b0101101)  `CHK("p5 fail", fm[5], 32'b0010000)
    // cover: $past(cnt)==2 && cnt==3 holds at tick 3 only
    `CHK("cover past", cv, 32'b0001000)
    if (errs == 0) $display("PASSED");
    $finish;
  end

  p1: assert property (@(posedge clk) a == b) pm[1] |= 1 << ($time/10); else fm[1] |= 1 << ($time/10);
  p2: assert property (@(posedge clk) a != b) pm[2] |= 1 << ($time/10); else fm[2] |= 1 << ($time/10);
  p3: assert property (@(posedge clk) cnt != 4'd3) pm[3] |= 1 << ($time/10); else fm[3] |= 1 << ($time/10);
  p4: assert property (@(posedge clk) $rose(b) |-> a) pm[4] |= 1 << ($time/10); else fm[4] |= 1 << ($time/10);
  p5: assert property (@(posedge clk) disable iff (cnt == 4'd2) cnt != 4'd4) pm[5] |= 1 << ($time/10); else fm[5] |= 1 << ($time/10);
  cp: cover property (@(posedge clk) $past(cnt) == 4'd2 && cnt == 4'd3) cv |= 1 << ($time/10);
endmodule
