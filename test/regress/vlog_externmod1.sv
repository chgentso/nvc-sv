extern module vlog_externmod1_inv #(parameter W = 4) (input [W-1:0] a,
                                                      output [W-1:0] y);
extern interface vlog_externmod1_if (input logic clk);

module vlog_externmod1_inv #(parameter W = 4) (input [W-1:0] a,
                                               output [W-1:0] y);
  assign y = ~a;
endmodule

interface vlog_externmod1_if (input logic clk);
  logic [3:0] v = 4'h5;
endinterface

module vlog_externmod1;
  logic clk = 0;
  wire [7:0] y;
  vlog_externmod1_inv #(8) u (8'h0f, y);
  vlog_externmod1_if i (clk);
  initial #1 $display("y=%h v=%h", y, i.v);
endmodule
