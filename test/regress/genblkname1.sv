// The name of an unnamed generate block is genblk<n> where n is the number
// of its generate construct in the enclosing scope: constructs are numbered
// from 1 in textual order whether their blocks are named or not, a nested
// scope numbers its own constructs, and zeros are put before the number
// when the name is already declared in the scope.  IEEE 1800-2017 27.6
// (and its example), 27.5.

module genblkname1;
   parameter genblk2 = 0;

   if (1) begin                        // genblk1
      logic a = 1;
   end

   if (genblk2) begin                  // genblk02 as genblk2 is declared
      logic a = 0;
   end
   else begin
      logic b = 1;
   end

   for (genvar i = 0; i < 1; i++) begin : g1     // Third construct
      if (1) begin                     // g1[0].genblk1
         logic c = 1;
      end
   end

   for (genvar i = 0; i < 1; i++)      // genblk4
      if (1) begin                     // genblk4[0].genblk1
         logic d = 1;
      end

   generate
      case (1)
        0: begin logic e = 0; end
        default: begin logic e = 1; end   // genblk5
      endcase
   endgenerate

   initial begin
      #1;
      if (genblk1.a !== 1) $fatal(1, "genblk1.a = %b", genblk1.a);
      if (genblk02.b !== 1) $fatal(1, "genblk02.b = %b", genblk02.b);
      if (g1[0].genblk1.c !== 1) $fatal(1, "g1[0].genblk1.c");
      if (genblk4[0].genblk1.d !== 1) $fatal(1, "genblk4[0].genblk1.d");
      if (genblk5.e !== 1) $fatal(1, "genblk5.e = %b", genblk5.e);
      $display("PASSED");
   end
endmodule
