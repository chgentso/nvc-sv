module ng_vm #(parameter W = 4, parameter [7:0] K = 8'h12,
            parameter integer N = -3)
  (input wire clk, output wire [W-1:0] q);
  assign q = {W{clk}};
endmodule
module ng_vx #(parameter string S = "dflt", parameter real R = 1.5,
               parameter type T = logic [3:0], parameter signed [7:0] SG = -2);
endmodule

module ng_vtop;
  ng_vx #(.S("a\"b"), .R(2.5), .T(int)) ux ();
  ng_vx ux_def ();
  reg clk = 0;
  wire [2:0] q0; wire [4:0] q1, q2;
  ng_vm #(.W(3), .K(8'hff), .S("hi")) u0 (.clk(clk), .q(q0));
  ng_vm #(5, 8'h01) u1 (clk, q1);
  for (genvar i = 0; i < 2; i++) begin : g
    wire [6:0] q;
    ng_vm #(.W(7), .N(i * 2), .R(2.0)) u (.clk(clk), .q(q));
  end
endmodule
