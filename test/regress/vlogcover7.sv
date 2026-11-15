// Branch coverage of case statements: casez, casex, case inside, several
// values for an item, default, a case without default which has no match,
// unique and priority case which have no such bin, and case on a string.

module vlogcover7;
   reg clk = 0;
   reg [2:0] sel = 0;
   reg [3:0] y;
   string s = "a";
   integer r;

   always #5 clk = ~clk;

   always @(posedge clk) begin
      sel <= sel + 1;

      // Matches for 0, 1 and 2, nothing for the rest
      case (sel)
         0: y = 1;
         1, 2: y = 2;
      endcase

      // Always matches an item
      case (sel)
         0: y = 3;
         default: y = 4;
      endcase

      // No match is an error here and is not a branch
      unique case (sel)
         0: r = 0;
         1: r = 1;
         2: r = 2;
      endcase

      priority case (sel)
         0: r = 4;
         1, 2: r = 5;
      endcase

      unique0 case (sel)
         0: r = 8;
         default: r = 9;
      endcase

      // Wild cards
      casez (sel)
         3'b1??: y = 5;
         3'b0?1: y = 6;
      endcase

      casex (sel)
         3'b00x: y = 7;
         3'bx11: y = 8;
         default: y = 9;
      endcase

      case (sel) inside
         [0:1]: y = 10;
         3, 5: y = 11;
      endcase

      // Matches every time
      case (sel)
         0, 1, 2, 3, 4, 5, 6, 7: y = 12;
      endcase

      case (s)
         "a": r = 6;
         "b": r = 7;
      endcase
   end

   initial begin
      #32;
      $display("PASSED");
      $finish;
   end
endmodule
