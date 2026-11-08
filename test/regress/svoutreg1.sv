// Output ports declared reg / logic in ANSI and non-ANSI styles, with initializers and drivers
// Expected values: IEEE 1800-2017 23.2.2.3 / 23.2.2.4 (port kinds and data types), 6.8 (default x, initializers),
// 9.2.2 (always @(posedge)), 10.4.2 (nonblocking), 10.3 (continuous assignment)
// q1,q2,q3 are driven by nonblocking assigns at the clock edge; q4 by an initial block; q5 by an initializer;
// q6 is never written (x); q7 is written at time 2; q8 by a continuous assignment.
`timescale 1ns/1ns
module c1(input clk, output reg [3:0] Q);
  always @(posedge clk) Q <= 1;
endmodule
module c2(clk, Q);
  input clk;
  output [3:0] Q;
  reg [3:0] Q;
  always @(posedge clk) Q <= 2;
endmodule
module c3(clk, Q);
  input clk;
  output reg [3:0] Q;
  always @(posedge clk) Q <= 3;
endmodule
module c4(clk, Q);
  input clk;
  output [3:0] Q;
  reg [3:0] Q;
  initial Q = 4;
endmodule
module c5(input clk, output reg [3:0] Q = 4'd5);
endmodule
module c6(input clk, output reg Q);
endmodule
module c7(input clk, output logic [3:0] Q);
  initial #2 Q = 7;
endmodule
module c8(input clk, output logic [3:0] Q);
  assign Q = 4'd6;
endmodule
module svoutreg1;
  reg clk = 0;
  wire [3:0] q1, q2, q3, q4, q5, q7;
  wire q6; wire [3:0] q8;
  int errs = 0;
  c1 u1(clk, q1); c2 u2(clk, q2); c3 u3(clk, q3); c4 u4(clk, q4);
  c5 u5(clk, q5); c6 u6(clk, q6); c7 u7(clk, q7); c8 u8(clk, q8);
  initial begin
    #1;
    if ({q1, q2, q3, q4, q5, q6, q7, q8} !== {4'bx, 4'bx, 4'bx, 4'd4, 4'd5, 1'bx, 4'bx, 4'd6}) begin
      $error("before clock: %b %b %b %b %b %b %b %b", q1, q2, q3, q4, q5, q6, q7, q8); errs++;
    end
    clk = 1;
    #2;
    if ({q1, q2, q3, q4, q5, q6, q7, q8} !== {4'd1, 4'd2, 4'd3, 4'd4, 4'd5, 1'bx, 4'd7, 4'd6}) begin
      $error("after clock: %b %b %b %b %b %b %b %b", q1, q2, q3, q4, q5, q6, q7, q8); errs++;
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
