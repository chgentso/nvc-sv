module m5v_calls #(parameter int N = 16)
  (input wire [$clog2(16)-1:0] a,
   output logic [$clog2(N+1)-1:0][3:0] b,
   input logic [(N>4 ? 8 : 4)-1:0] c, input wire [3:0] d);
endmodule
