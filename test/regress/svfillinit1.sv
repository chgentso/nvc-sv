// Unbased unsized literals as variable initializers and assigned values.
// Expected values: IEEE 1800-2017 5.7.1 ('0, '1, 'x, 'z fill every bit of the target),
// 6.8 (initializer value is in effect at time 0).
module svfillinit1;
  reg [7:0] a = 'z;
  reg [7:0] b = 'x;
  reg [7:0] c = '1;
  reg [7:0] d = '0;
  reg [7:0] e;
  initial begin
    e = 'z;
    #1;
    if (a !== 8'bzzzzzzzz) $error("a = %b", a);
    else if (b !== 8'bxxxxxxxx) $error("b = %b", b);
    else if (c !== 8'b11111111) $error("c = %b", c);
    else if (d !== 8'b00000000) $error("d = %b", d);
    else if (e !== 8'bzzzzzzzz) $error("e = %b", e);
    else $display("PASSED");
  end
endmodule
