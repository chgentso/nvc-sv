// A net implicitly declared by a port connection may be used earlier in
// the module (IEEE 1800-2017 6.10)
module svimplnet1;
  logic x = 1;
  wire [1:0] y;
  logic z;
  assign y = {led, x};
  always_comb z = led2;
  sub u1 (.o(led));
  sub u2 (.o(led2));
  initial begin
    #1;
    if (y === 2'b11 && z === 1'b1) $display("PASSED");
    else $display("FAILED %b %b", y, z);
  end
endmodule

module sub(output o);
  assign o = 1'b1;
endmodule
