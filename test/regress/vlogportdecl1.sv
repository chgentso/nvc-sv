// Non-ANSI port declarations may have unpacked dimensions
// (IEEE 1800-2017 23.2.2.1), and an ANSI port may be declared with var
// (23.2.2.3, 6.5).

module sub (in, mem, flag, val);
   input [3:0] in;
   output reg [3:0] mem [0:3];
   input flag [1:0];
   output reg [3:0] val = 4'd9;

   integer i;
   always @* begin
      for (i = 0; i < 4; i++)
         mem[i] = in + i;
   end
endmodule

module sub2 (input var logic [3:0] x, input var logic y [1:0],
             output var done = 1'b1);
endmodule

module vlogportdecl1;
   reg [3:0] in = 4'd5;
   wire [3:0] mem [0:3];
   wire [3:0] val;
   wire flag [1:0];
   wire done;
   reg yy [1:0];

   sub u (in, mem, flag, val);
   sub2 u2 (in, yy, done);

   initial begin
      #1;
      if (mem[0] !== 4'd5 || mem[3] !== 4'd8) $fatal(1, "mem %0d %0d", mem[0], mem[3]);
      if (val !== 4'd9) $fatal(1, "val %0d", val);
      if (done !== 1'b1) $fatal(1, "done %b", done);
      $display("PASSED");
   end
endmodule
