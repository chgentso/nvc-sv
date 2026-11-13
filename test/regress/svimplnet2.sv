`default_nettype none
module svimplnet2;
  logic x = 1;
  wire [1:0] y;
  assign y = {led, x};
  sub u1 (.o(led));
endmodule

module sub(output wire o);
  assign o = 1'b1;
endmodule
