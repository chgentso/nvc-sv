module g0decl import pk::*; (input logic clk, go);
  st_e state_q, state_d;
  foo_t v1, v2 = 3;
  foo_t arr [3];
  logic [3:0] x, y;
  bar_t #(4) inst (.a(go));
  always_ff @(posedge clk) state_q <= go ? state_d : v1;
endmodule
