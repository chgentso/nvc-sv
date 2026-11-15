// Procedural continuous assignments, force and release are rewritten into
// several statements and processes by the parser.  Only the statements of
// the source are items.  The statement of a force or assign is also run by
// the process which repeats it when an operand changes so it counts twice.

module vlogcover10;
   reg [3:0] r, s;
   reg [3:0] a = 5, b = 3;

   initial begin
      r = 1;
      force r = a;
      #1 release r;
      assign s = b;
      #1 deassign s;
      if (r === 4'd1) begin
         force r = b;
         release r;
      end
      $display("PASSED");
   end
endmodule
