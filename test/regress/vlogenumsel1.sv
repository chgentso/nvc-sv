// A bit or part of the name of an enumeration constant can be selected,
// also in a continuous assignment (IEEE 1800-2017 6.19).

module vlogenumsel1;
   enum logic [6:0] { ENUM_VAL = 7'h33, OTHER = 7'h05 } e;

   wire bit0 = ENUM_VAL[0];
   wire [2:0] mid = ENUM_VAL[5:3];
   wire hi = OTHER[2];

   initial begin
      #1;
      if (bit0 !== 1'b1 || mid !== 3'b110 || hi !== 1'b1)
         $fatal(1, "%b %b %b", bit0, mid, hi);
      if (ENUM_VAL[1] !== 1'b1 || OTHER[1] !== 1'b0)
         $fatal(1, "procedural");
      $display("PASSED");
   end
endmodule
