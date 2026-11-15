module linterr17 #(parameter int P = 1)
    input  wire clk,
    input  wire rst,
    output wire q
);
  assign q = clk & rst;
endmodule
