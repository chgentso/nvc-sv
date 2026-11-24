`timescale 1ns/1ns

module vpi_packed1;
   logic [7:0]   s8;
   bit   [15:0]  b16;
   logic [127:0] w128;
   bit   [99:0]  b100;

   initial begin
      s8   = 8'b10zx_01xz;
      b16  = 16'hcafe;
      w128 = {64'hdead_beef_1234_5678, 64'h9abc_def0_0fed_cba9};
      b100 = {4'h5, 32'h1122_3344, 64'h5566_7788_99aa_bbcc};

      #15;
      if (s8 !== 8'b0101_xz10) $fatal(1, "s8=%b", s8);
      if (b16 !== 16'h00aa) $fatal(1, "b16=%h", b16);
      if (w128 !== {64'h0123_4567_89ab_cdef, 64'hfedc_ba98_7654_3210})
         $fatal(1, "w128=%h", w128);
      if (b100 !== {4'ha, 32'h0000_0000, 64'h1234_5678_9abc_def0})
         $fatal(1, "b100=%h", b100);

      #5;
      s8 = 8'haa; // Masked out while forced
      #5;
      if (s8 !== 8'h3c) $fatal(1, "s8 forced=%h", s8);
      #10;
      s8 = 8'h55;
      #1;
      if (s8 !== 8'h55) $fatal(1, "s8 released=%h", s8);
      $display("PASSED");
   end
endmodule
