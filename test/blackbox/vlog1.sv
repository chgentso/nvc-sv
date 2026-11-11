module vlog1;
  logic clk;
  wire [7:0] q;
  wire [7:0] a, b;
  wire ready;
  logic [1:0] sel;

  // Named connections: the ports are those named, the widths those of the actuals
  cellbuf #(.W(8), .DEPTH(2)) u_buf1 (.i(a), .o(b)), u_buf2 (.i(b), .o(a));

  // Positional connections: the ports are p0, p1, ...
  cellpll u_pll (clk, ready, sel);

  // Unconnected and part-selected
  cellmux u_mux (.d(a[3:0]), .s(sel), .y(), .oe(1'b1));

  assign q = b;
endmodule
