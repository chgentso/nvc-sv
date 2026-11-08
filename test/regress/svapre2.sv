// Self-checking. Expected values: IEEE 1800-2017 16.5.1 (sampled values), 16.12.1/16.14.1 (vacuous success
// runs the pass action; |=> evaluates the consequent at the next tick), 16.15 (disable iff uses the current
// value, i.e. after the NBA update of dis), 16.5.1 (continuous nets and arrays are sampled consistently).
// Clock edges are at t=5,15,25,... ; bit k of a mask is set by the attempt finishing at t=10*k+5.
`timescale 1ns/1ns
`define CHK(tag, got, exp) if ((got) !== (exp)) begin errs++; $error("%s: got %0d expected %0d", tag, got, exp); end
module svapre2;
  int errs = 0;
  logic clk = 0;
  logic [3:0] cnt = 0;
  logic dis = 0;
  wire [3:0] w4 = cnt + 1;
  logic [3:0] mem [0:1];
  int pm[6:9], fm[6:9];
  always #5 clk = ~clk;
  always @(posedge clk) cnt = cnt + 1;
  logic [3:0] tk = 0;
  always @(posedge clk) tk <= tk + 1;
  always @(posedge clk) dis <= (tk == 4'd2);
  always @(posedge clk) begin mem[0] = cnt; mem[1] = cnt + 4'd1; end
  initial begin
    mem[0] = 0; mem[1] = 1;
    for (int i = 6; i <= 9; i++) begin pm[i] = 0; fm[i] = 0; end
    #62;
    // p6: sampled cnt at tick k is k; dis becomes 1 (NBA) at tick 2, so tick 2 is disabled;
    //     cnt != 4 fails at tick 4
    `CHK("p6 pass", pm[6], 32'b0101011)  `CHK("p6 fail", fm[6], 32'b0010000)
    // p7, p8: sampled values are consistent, always true
    `CHK("p7 pass", pm[7], 32'b0111111)  `CHK("p7 fail", fm[7], 32'b0)
    `CHK("p8 pass", pm[8], 32'b0111111)  `CHK("p8 fail", fm[8], 32'b0)
    // p9: cnt[0] holds at ticks 1,3,5. Ticks 0,2,4 end vacuously (pass at 0,2,4); the attempts of ticks 1 and 3
    //     resolve at ticks 2 and 4 with success; the attempt of tick 5 is unresolved when the run ends
    `CHK("p9 pass", pm[9], 32'b0010101)  `CHK("p9 fail", fm[9], 32'b0)
    if (errs == 0) $display("PASSED");
    $finish;
  end

  p6: assert property (@(posedge clk) disable iff (dis) cnt != 4'd4) pm[6] |= 1 << ($time/10); else fm[6] |= 1 << ($time/10);
  p7: assert property (@(posedge clk) w4 == cnt + 4'd1) pm[7] |= 1 << ($time/10); else fm[7] |= 1 << ($time/10);
  p8: assert property (@(posedge clk) mem[1] == mem[0] + 4'd1) pm[8] |= 1 << ($time/10); else fm[8] |= 1 << ($time/10);
  p9: assert property (@(posedge clk) cnt[0] |=> !cnt[0] || cnt == 4'd5) pm[9] |= 1 << ($time/10); else fm[9] |= 1 << ($time/10);
endmodule
