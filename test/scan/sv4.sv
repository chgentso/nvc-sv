module m1 (input logic clk, ext_if.mp p, other_if q);
  ext2_if bi ();
  logic x;
  always @(posedge clk) x <= p.data;
endmodule
