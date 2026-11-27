// VPI exposure of genvar localparams inside shared for-generate scopes
// IEEE 1800-2017 27.4, 37.3
module vpi_genvar1;
   reg [3:0] d = 4'h5;
   wire [3:0] q;

   genvar idx;
   generate
      for (idx = 4; idx <= 7; idx = idx + 1) begin : gblk
         localparam int BASE = 4;
         assign q[idx - BASE] = d[idx - BASE];
      end
   endgenerate

   initial begin
      #1;
      if (q !== 4'h5)
         $fatal(1, "mismatch: %b", q);
      $finish;
   end
endmodule
