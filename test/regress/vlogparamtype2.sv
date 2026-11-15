// A parameter with no type declared takes the type of the value given for
// it, including a localparam with a range (IEEE 1800-2017 6.20.2).

module sub #(parameter WIDTH = 10, parameter signed S = 1)
   (output int o);
   localparam MSB = WIDTH - 1;
   assign o = $bits(WIDTH) * 100 + $bits(MSB);
endmodule

module vlogparamtype2;
   localparam logic [4:0] WID = 16;
   localparam logic [7:0] W8 = 8'hff;
   int o1, o2, o3;

   sub #(WID) s1 (o1);
   sub #(.WIDTH(W8)) s2 (o2);
   sub #(.WIDTH(12)) s3 (o3);

   initial begin
      #1;
      if (o1 !== 532 || o2 !== 832 || o3 !== 3232)
         $fatal(1, "bits %0d %0d %0d", o1, o2, o3);
      $display("PASSED");
   end
endmodule
