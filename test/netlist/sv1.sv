module inner #(parameter W = 4) (input [W-1:0] a, output [W-1:0] y,
                                 inout io);
  assign y = ~a;
endmodule

module sv1;
  logic clk = 0;
  logic [7:0] w;
  wire [3:0] hi, lo;
  wire t;
  logic [3:0] r;

  always #5 clk = ~clk;

  always_ff @(posedge clk) r <= w[3:0];

  inner i0 (.a(w[3:0]), .y(lo), .io(t));
  inner i1 (.a({w[7], w[6], w[5], w[4]}), .y(hi), .io(t));
  inner i2 (.a(4'b1010), .y(), .io());

  generate
    for (genvar k = 0; k < 2; k++) begin : gg
      inner ig (.a(w[k +: 4]), .y(), .io());
    end
  endgenerate
endmodule
