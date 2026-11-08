// Unknown bits of a four-state actual become zero in a two-state input port (self-checking).
// Expected values: IEEE 1800-2017 23.3.3 (port connection semantics: an implicit continuous assignment from the
// actual to the formal), 6.11.3 / 11.8.1 (x and z are converted to 0 when a four-state value is assigned to a
// two-state variable), 6.8 (an uninitialised logic is x).
// Reference model: u8 = 5a is passed unchanged; u65 and ux are all x -> all-zero ports of 65, 100 and 66
// bits; mix = 8'b10xz01x1 -> 8'b10000101.
`timescale 1ns/1ns
module svbitport1_sub #(parameter W = 1, parameter ID = 0, parameter logic [W-1:0] EXP = '0) (input bit [W-1:0] d);
  int bad = 0;
  initial begin
    #ID;
    if (d !== EXP) begin bad++; $error("sub %0d (W=%0d): d=%b, expected %b", ID, W, d, EXP); end
  end
endmodule

module svbitport1;
  logic [99:0] ux;
  logic [7:0] u8 = 8'h5a;
  logic [64:0] u65;
  logic [7:0] mix = 8'b10xz01x1;
  svbitport1_sub #(8, 1, 8'h5a) s1(u8);
  svbitport1_sub #(65, 2, 65'd0) s2(u65);
  svbitport1_sub #(100, 3, 100'd0) s3(ux);
  svbitport1_sub #(66, 4, 66'd0) s4(ux[65:0]);
  svbitport1_sub #(8, 5, 8'b10000101) s5(mix);
  initial begin
    #10;
    if (s1.bad + s2.bad + s3.bad + s4.bad + s5.bad == 0) $display("PASSED");
  end
endmodule
