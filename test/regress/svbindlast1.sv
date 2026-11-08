// Several bind directives into one module, declared after the module, plus ordinary instances (self-checking).
// Expected values: IEEE 1800-2017 23.11 (each bind adds one instance to every instance of the target module;
// the bound instances are ordinary instances reachable by name), 9.4.2 (@posedge), 4.7 (processes triggered
// by one event run in unspecified order, so only the per-process results are checked).
// Reference model: clk rises once, at t=5.  Every one of the five mon instances (m0,m1 and the bound b1..b3)
// and the always block of dut observes that edge exactly once, at t=5, with its own id.
`timescale 1ns/1ns
module mon(input logic clk, input int id);
  int hits = 0;
  longint when_hit = -1;
  int my_id = -1;
  always @(posedge clk) begin hits++; when_hit = $time; my_id = id; end
endmodule
module dut(input logic clk);
  int dhits = 0;
  longint dwhen = -1;
  mon m0(clk, 0);
  always @(posedge clk) begin dhits++; dwhen = $time; end
  mon m1(clk, 1);
endmodule
bind dut mon b1(clk, 11);
bind dut mon b2(clk, 12);
bind dut mon b3(clk, 13);
module svbindlast1;
  logic clk = 0;
  int errs = 0;
  dut d(clk);
  initial begin #5 clk = 1; end
  initial begin
    #10;
    if (d.dhits != 1 || d.dwhen != 5) begin errs++; $error("dut always: %0d hits at %0d", d.dhits, d.dwhen); end
    if (d.m0.hits != 1 || d.m0.when_hit != 5 || d.m0.my_id != 0) begin errs++; $error("m0"); end
    if (d.m1.hits != 1 || d.m1.when_hit != 5 || d.m1.my_id != 1) begin errs++; $error("m1"); end
    if (d.b1.hits != 1 || d.b1.when_hit != 5 || d.b1.my_id != 11) begin errs++; $error("b1"); end
    if (d.b2.hits != 1 || d.b2.when_hit != 5 || d.b2.my_id != 12) begin errs++; $error("b2"); end
    if (d.b3.hits != 1 || d.b3.when_hit != 5 || d.b3.my_id != 13) begin errs++; $error("b3"); end
    if (errs == 0) $display("PASSED");
  end
endmodule
