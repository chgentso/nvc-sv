module m5g_genloop;
  reg clk = 0;
  wire [1:0] q;
  genvar i;
  generate
    for (i = 0; i < 2; i = i + 1) begin : g
      reg r;
      initial r = 0;
      always @(posedge clk) r <= ~r;
      assign q[i] = r;
    end
  endgenerate
endmodule
