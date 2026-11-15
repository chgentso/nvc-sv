// A parameter declared with only a signing takes the range of its value but
// the signing given, and a parameter with no type takes the type of its
// value including a real value (IEEE 1800-2017 6.20.2).

module vlogparamtype1;
   localparam unsigned [63:0] UNSIGNED = 64'h99934567_89abcdef;
   localparam signed [63:0] SIGNED = 64'sh99934567_89abcdef;
   localparam real REAL = 1.234;

   localparam A_REAL = REAL;
   localparam signed C_SIGNED = UNSIGNED;
   localparam unsigned C_UNSIGNED = SIGNED;
   localparam signed S8 = 8'hff;
   localparam unsigned U8 = 8'sh80;

   initial begin
      if (A_REAL != 1.234) $fatal(1, "A_REAL %f", A_REAL);
      if ($bits(C_SIGNED) != 64 || !(C_SIGNED < 0))
         $fatal(1, "C_SIGNED %0d %0d", $bits(C_SIGNED), C_SIGNED < 0);
      if ($bits(C_UNSIGNED) != 64 || !(C_UNSIGNED > 0))
         $fatal(1, "C_UNSIGNED %0d", $bits(C_UNSIGNED));
      if ($bits(S8) != 8 || !(S8 < 0)) $fatal(1, "S8");
      if ($bits(U8) != 8 || !(U8 > 0)) $fatal(1, "U8");
      $display("PASSED");
   end
endmodule
