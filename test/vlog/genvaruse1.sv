// IEEE 1800-2017 27.4: a genvar is only used in its generate loop.
module genvaruse1 #(parameter N = 4) (input clk, input [7:0] iv [N-1:0],
                                      output logic [7:0] ov [N-1:0]);
  genvar i, j;
  for (i = 0; i < N; i++) begin : g           // OK
    always @(posedge clk) ov[i] <= iv[i];
  end

  always @(posedge clk) ov[0] <= iv[j];       // Error
endmodule
