module wn4_v (input wire [1:0] a, output wire [1:0] b);
  wire [1:0] t;
  assign t = ~a;
  assign b = t;
endmodule
