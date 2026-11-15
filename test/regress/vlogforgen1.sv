// The step of a generate loop may be an operator assignment
// (IEEE 1800-2017 27.4, genvar_iteration).

module vlogforgen1;
   wire [31:0] w;
   genvar g, h, k;

   for (g = 0; g < 8; g += 2) begin : a
      assign w[g] = 1'b1;
      assign w[g + 1] = 1'b0;
   end

   for (h = 16; h > 0; h -= 4) begin : b
      assign w[h + 7] = 1'b1;
   end

   for (k = 1; k < 100; k *= 3) begin : c
      localparam int V = k;
   end

   initial begin
      #1;
      if (w[7:0] !== 8'b01010101) $fatal(1, "a %b", w[7:0]);
      if (w[23] !== 1'b1 || w[19] !== 1'b1 || w[15] !== 1'b1
          || w[11] !== 1'b1 || w[22] !== 1'bz)
         $fatal(1, "b %b", w[23:8]);
      if (c[27].V != 27) $fatal(1, "c %0d", c[27].V);
      $display("PASSED");
   end
endmodule
