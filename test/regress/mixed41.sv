// BOOLEAN ports of a VHDL entity connected to Verilog nets
module mixed41;
   reg c = 1'b1;
   wire d;

   mixed41_sub u (.en(c), .o(d));

   initial begin
      #1;
      if (d !== 1'b0) begin
         $display("FAILED %b", d);
         $finish;
      end
      c = 1'b0;
      #1;
      if (d !== 1'b1)
        $display("FAILED %b", d);
      else
        $display("PASSED");
      $finish;
   end
endmodule
