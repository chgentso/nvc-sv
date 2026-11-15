// Branch coverage of the conditional operator: in a continuous assignment,
// in procedural code, nested, in a function, with real and string operands
// and with a condition which is unknown.  An expression which is constant
// has no branch.

module vlogcover8;
   parameter P = 1 ? 2 : 3;
   localparam [3:0] L = P > 1 ? 4'd5 : 4'd6;

   reg clk = 0;
   reg [1:0] sel = 0;
   reg [3:0] a = 1, b = 2, y, z;
   real r;
   string s;
   reg x = 1'bx;

   always #5 clk = ~clk;

   wire [3:0] w = sel[0] ? a : b;       // Net declaration
   wire [3:0] v;
   assign v = sel == 3 ? 4'd1 : 4'd0;   // Continuous assignment

   function int pick(input bit c);
      return c ? 10 : 20;               // In a function
   endfunction

   always @(posedge clk) begin
      sel <= sel + 1;
      y = sel[1] ? (sel[0] ? 4'd1 : 4'd2) : 4'd3;   // Nested
      r = sel == 0 ? 1.5 : 2.5;                      // Real
      s = sel == 1 ? "one" : "other";                // String
      z = x ? 4'd1 : 4'd2;                           // Unknown
      z = pick(sel[0]);
      z = L;                                          // Constant
   end

   // A property is checked by processes the coverage does not count and the
   // conditional operator in it is not evaluated as a value
   assert property (@(posedge clk) sel[0] ? a != 0 : b != 0);

   initial begin
      #32;
      $display("PASSED");
      $finish;
   end
endmodule
