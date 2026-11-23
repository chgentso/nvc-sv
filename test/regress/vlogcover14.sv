// Toggle coverage of memories with include-mems, and the limit on the size
// of a vector.  A vector with 8 or more bits is skipped by the limit of 8.

module vlogcover14;
   reg [1:0] mem [0:2];     // Counted: 6 bits
   reg [1:0] desc [3:1];    // Counted, index runs down
   reg [7:0] wide;          // 8 bits: skipped by the limit
   reg [6:0] ok;            // 7 bits: counted
   reg       two [0:1][0:1];   // Not counted: two dimensions

   initial begin
      mem[0] = 0; mem[1] = 0; mem[2] = 0;
      desc[1] = 0; desc[2] = 0; desc[3] = 0;
      wide = 0; ok = 0; two[0][0] = 0;
      #1 mem[1] = 2'b10; desc[3] = 2'b01; wide = 8'hff; ok = 7'h40;
      #1 mem[1] = 2'b00; desc[2] = 2'b11;
      #1 $display("PASSED");
      $finish;
   end
endmodule
