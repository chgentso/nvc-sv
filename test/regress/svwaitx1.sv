// A wait statement does not continue for an unknown condition
// Expected values: IEEE 1800-2017 9.4.3 (wait continues only when the condition is true),
// 12.4 (an expression is true if it is non-zero; a vector with a known 1 bit is true even with x
// bits, a value that is all x or z is not true), 11.4.5 (== with x gives x).
`timescale 1ns/1ns
module svwaitx1;
  wire p;
  logic d = 1'bx;
  assign p = d;
  logic [3:0] vx = 4'bx;
  logic [3:0] v1 = 4'b0100;
  logic [3:0] v2 = 4'b01x0;
  logic x2;
  logic [5:0] done = 0;
  int errors = 0;
  initial begin
    #1;
    fork
      begin wait(p); done[0] = 1; end
      begin wait(d); done[1] = 1; end
      begin wait(vx); done[2] = 1; end
      begin wait(x2); done[3] = 1; end
      begin wait(p == 1'b1); done[4] = 1; end
      begin wait(v1); wait(v2); done[5] = 1; end
    join_none
    // only the vector waits that have a known 1 bit pass
    #10;
    if (done !== 6'b100000) begin $error("t=11 done=%b exp 100000", done); errors++; end
    d = 1'b0;
    #10;
    if (done !== 6'b100000) begin $error("t=21 done=%b exp 100000", done); errors++; end
    d = 1'b1; vx = 4'b0010; x2 = 1;
    #10;
    if (done !== 6'b111111) begin $error("t=31 done=%b exp 111111", done); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
