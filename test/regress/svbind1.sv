// bind of a checker module into every instance of a module (self-checking).
// Expected values: IEEE 1800-2017 23.11 (bind directive: the bound instance becomes part of each target
// instance and can see its ports and nets; it is reachable hierarchically), 9.2.2.4 (always_ff),
// 4.7/9.4.2 (@posedge), 6.8 (int initialises to 0).
// Reference model: clk toggles 20 times from 0 (rising edges at 5,15,...,95, 10 of them); rst is high until 12,
// so q is cleared at the edge 5 and incremented at 15,25,...,95 (9 increments): q == q2 == 9.  The bound
// checker (one per counter instance) sees every edge and must never see nxt != q + 1 when !rst.
module counter #(parameter int W = 4) (input logic clk, input logic rst, output logic [W-1:0] q);
  logic [W-1:0] nxt;
  always_comb nxt = q + 1'b1;
  always_ff @(posedge clk) if (rst) q <= '0; else q <= nxt;
endmodule

module cnt_chk #(parameter int N = 1) (input logic clk, input logic rst, input logic [3:0] q, input logic [3:0] nxt);
  int ticks;
  int bad;
  always @(posedge clk) begin
    ticks <= ticks + 1;
    if (!rst && q != 4'hf && nxt !== q + N) bad <= bad + 1;
  end
endmodule

bind counter cnt_chk #(.N(1)) u_chk (.clk(clk), .rst(rst), .q(q), .nxt(nxt));

module svbind1;
  logic clk = 0, rst = 1;
  logic [3:0] q, q2;
  int errs = 0;
  counter c1(clk, rst, q);
  counter #(.W(4)) c2(.clk(clk), .rst(rst), .q(q2));
  initial begin
    repeat (20) #5 clk = ~clk;
    #1;
    if (q !== 4'd9 || q2 !== 4'd9) begin errs++; $error("q=%0d q2=%0d, expected 9 9", q, q2); end
    if (c1.u_chk.ticks != 10 || c2.u_chk.ticks != 10) begin errs++; $error("checker ticks %0d %0d, expected 10", c1.u_chk.ticks, c2.u_chk.ticks); end
    if (c1.u_chk.bad != 0 || c2.u_chk.bad != 0) begin errs++; $error("checker reported mismatches %0d %0d", c1.u_chk.bad, c2.u_chk.bad); end
    if (errs == 0) $display("PASSED");
  end
  initial #12 rst = 0;
endmodule
