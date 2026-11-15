// Verilog modules under a VHDL top level and around a VHDL entity
module ev_sync2 (input wire clk, input wire d, output wire q);
  (* ASYNC_REG = "TRUE" *) reg s0, s1;
  always @(posedge clk) begin
    s0 <= d;
    s1 <= s0;
  end
  assign q = s1;
endmodule

module ev_vtop (input wire clk_a, clk_b, input wire d, output wire q);
  reg r;
  wire m;
  always @(posedge clk_a) r <= d;
  ev_vleaf u_leaf (.clk(clk_a), .d(r), .q(m));
  ev_sync2 u_sync (.clk(clk_b), .d(m), .q(q));
endmodule
