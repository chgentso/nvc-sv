// Expected values: IEEE 1800-2017 23.3.3.5 (a 12-bit actual split over a 3-instance array of 4-bit ports,
// the output actual split the same way); the leaf (VHDL) inverts its input, so y = ~x.
module svinstarr3;
  reg [11:0] x = 12'h123;
  wire [11:0] y;
  svinstarr3_leaf l[2:0] (.a(x), .y(y));
  initial begin
    #1;
    if (y === 12'hedc) $display("PASSED");
    else $display("ERROR: y=%h expected edc", y);
  end
endmodule
