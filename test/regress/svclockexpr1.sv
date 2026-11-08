// Expected values: IEEE 1800-2017 14.3 (clockvar with an expression: `output lo = wide[3:0]` drives
// that part select; `input d_in = d_int` samples that expression), 14.4 / 14.16 (output skew
// defaults to #0; an off-event drive waits for the next clocking event; input clockvars hold
// the value sampled in the Preponed region of the event), 21.2.1.
// Clock edges at 5, 15, ...  Drives issued at 0 and 12 take effect at edges 5 and 15.
`timescale 1ns/1ns
interface bus_if(input logic clk);
  logic [3:0] d_int;
  logic [3:0] d;
  logic [7:0] wide;
  logic ok_int;
  assign d = d_int;
  clocking cb @(posedge clk);
    output d = d_int;
    output lo = wide[3:0];
    output hi = wide[7:4];
    output bit0 = wide[0];
    input  d_in = d_int;
    output ok = ok_int;
  endclocking
endinterface
module svclockexpr1;
  logic clk = 0;
  bus_if i(clk);
  int errors = 0;
  always #5 clk = ~clk;
  initial begin
    i.cb.d <= 4'd5;
    i.cb.lo <= 4'h3;
    i.cb.hi <= 4'hc;
    i.cb.ok <= 1'b1;
    #12;
    if (i.d_int !== 4'd5) begin $error("d_int = %0d, expected 5", i.d_int); errors++; end
    if (i.wide !== 8'hc3) begin $error("wide = %h, expected c3", i.wide); errors++; end
    if (i.ok_int !== 1'b1) begin $error("ok_int = %b, expected 1", i.ok_int); errors++; end
    if (i.d !== 4'd5) begin $error("d = %0d, expected 5 (assign d = d_int)", i.d); errors++; end
    i.cb.bit0 <= 1'b0;
    @(i.cb);
    #1;
    if (i.cb.d_in !== 4'd5) begin $error("d_in = %0d, expected 5", i.cb.d_in); errors++; end
    if (i.wide !== 8'hc2) begin $error("wide = %h, expected c2", i.wide); errors++; end
    #10;
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
