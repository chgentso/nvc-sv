// Port bounds of a VHDL entity that call functions of generics
module mixed38;
   wire [15:0] d1;
   wire [7:0] d2;
   wire [4:0] a1;
   wire [3:0] a2;

   mixed38_sub #(.g_wide(1'b1), .g_depth(20)) u1 (.d(d1), .a(a1));
   mixed38_sub u2 (.d(d2), .a(a2));

   initial begin
      #1;
      if (d1 !== 16'hffff || d2 !== 8'hff || a1 !== 5'h1f || a2 !== 4'hf)
        $display("FAILED %h %h %h %h", d1, d2, a1, a2);
      else
        $display("PASSED");
      $finish;
   end
endmodule
