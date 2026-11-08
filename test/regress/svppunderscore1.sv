// Macro formal arguments starting with underscores, nested macros with defaults, number literals
// with underscores, always_ff with async reset.
// Expected values: IEEE 1800-2017 22.5.1 (macro args/defaults), 5.7.1 (underscores in numbers),
// 9.4.2 (edge events), 11.4.9 (logical not), 11.3 (arithmetic)
string got = "";
`define F(__x, __y) got = $sformatf("%0d %0d %0d %0d", !__x, -__y, 1_0 + __y, ~__x & 1);
`define FLOP(__d, __q, __clk = `CLK, __rst_n = `RST) \
  always_ff @(posedge __clk or negedge __rst_n) begin \
    if (!__rst_n) __q <= 0; else __q <= __d; \
  end
`define CLK clk
`define RST rst_n
`define SPARSE(__d, __q, __clk = `CLK, __rst_n = `RST) \
  `ifdef SIMULATION \
    `FLOP(__d, __q, __clk, __rst_n) \
  `else \
    `FLOP(__d, __q, __clk, __rst_n) \
  `endif
`timescale 1ns/1ns
module svppunderscore1;
  logic clk = 0, rst_n = 0, d = 1, q, q2;
  logic [3:0] a = 4'b1_01;
  int errs = 0;
  `SPARSE(d, q)
  `FLOP(d, q2, clk, rst_n)
  always #5 clk = ~clk;
  initial begin
    `F(0, 3);
    if (got != "1 -3 13 1") begin errs++; $error("F got '%s'", got); end
    if (8'b1000_0001 !== 129) begin errs++; $error("literal"); end
    if (a !== 4'd5) begin errs++; $error("a=%b", a); end
    #3 rst_n = 1;
    #20;
    // posedge clk at 5 and 15 with reset released at 3: both flops captured d=1
    if (q !== 1'b1 || q2 !== 1'b1) begin errs++; $error("q=%b q2=%b", q, q2); end
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
