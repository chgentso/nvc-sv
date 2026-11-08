module svxlib1_vm #(parameter int W = 4) (input logic clk, input logic [W-1:0] d, output logic [W-1:0] q);
  always @(posedge clk) q <= d;
endmodule
