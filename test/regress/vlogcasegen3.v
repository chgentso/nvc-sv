// A case generate construct may appear directly as a module item, without
// generate and endgenerate (IEEE 1364-2005 12.4.2)

module vlogcasegen3;
   parameter P = 2;
   wire [3:0] w;

   case (P)
      1: assign w = 4'd1;
      2: assign w = 4'd2;
      default: assign w = 4'd0;
   endcase

   initial begin
      #1;
      if (w !== 4'd2) $display("FAILED w = %d", w);
      else $display("PASSED");
   end
endmodule
