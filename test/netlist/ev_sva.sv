// Concurrent assertions are cells that check and drive nothing
module ev_sva;
  logic clk, b;
  logic [3:0] a, r, q;
  always_ff @(posedge clk) r <= a;
  always_ff @(posedge clk) q <= r;
  assert property (@(posedge clk) b |-> r == 4'd3);
  p1: assume property (@(posedge clk) disable iff (b) a[0] |=> q[1]);
endmodule
