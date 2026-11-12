module leaf #(parameter int W = 4,
              parameter logic [3:0] INIT = 4'ha,
              parameter signed [7:0] SV = 8'sh80,
              parameter logic [3:0] XZ = 4'b10xz,
              parameter string S = "hi",
              parameter real R = 0.5,
              parameter type T = logic [1:0],
              parameter UT = 7,
              localparam int L = W * 2)
  (input logic [W-1:0] a, output logic [L-1:0] y);
  assign y = {a, a};
endmodule

module mid #(parameter int N = 2) (input logic [N-1:0] a, output logic [N*2-1:0] y);
  leaf #(.W(N)) u (.a(a), .y(y));
endmodule

module top;
  logic [1:0] x;
  logic [3:0] y0, y1, y2;
  logic [7:0] y3, y4, y5;
  genvar i;
  leaf lu (.a(x[0]), .y(y0[1:0]));
  leaf #(3, 4'h3, -1, 4'bzzzz, "abc", 1e-9, int, 12) lo (.a(x[0]), .y());
  mid #(.N(2)) mu (.a(x), .y(y1));
  leaf dp (.a(x[0]), .y());
  defparam dp.W = 5;
  for (i = 1; i < 3; i++) begin : g
    leaf #(.W(i)) u (.a(x[0]), .y());
  end
endmodule
