// Writing to and reading from VHDL signals by hierarchical name
`timescale 1ns/1ns
module mixed32;
   reg d = 1'bz;
   reg [3:0] dv = 4'bzzzz;
   reg top_in = 0;
   integer i, n;
   reg [31:0] wide;
   reg bb;
   reg [1:0] bv;
   wire [3:0] o;

   mixed32_sub u (.i_port(top_in), .o_port(o));

   assign u.res = d;
   assign u.vres[2:1] = dv[1:0];

   `define CHECK(x, y) if ((x) !== (y)) begin \
      $display("FAILED line %0d: %b", `__LINE__, (x)); $finish; end

   initial begin
      // Blocking write to a scalar: the VHDL side sees the event
      u.b = 1'b1;
      `CHECK(u.b, 1'b1);
      #1 `CHECK(u.b_seen, 1'b1);

      // Non-blocking write takes effect later
      u.b <= 1'b0;
      `CHECK(u.b, 1'b1);
      #1 `CHECK(u.b, 1'b0);
      `CHECK(u.b_seen, 1'b0);

      // Each Verilog value maps to its STD_LOGIC value and back
      u.b = 1'bz;
      `CHECK(u.b, 1'bz);
      u.b = 1'bx;
      `CHECK(u.b, 1'bx);

      // STD_LOGIC values with no direct Verilog equivalent
      `CHECK(u.h, 1'b1);
      `CHECK(u.l, 1'b0);
      `CHECK(u.u, 1'bx);

      // Vectors
      u.v = 8'ha5;
      `CHECK(u.v, 8'ha5);
      #1 `CHECK(u.v_copy, 8'ha5);

      // Bit select, part select and a variable index
      u.v[7] = 1'b0;
      `CHECK(u.v, 8'h25);
      u.v[3:0] = 4'h3;
      `CHECK(u.v, 8'h23);
      `CHECK(u.v[5:4], 2'b10);
      i = 6;
      u.v[i] = 1'b1;
      `CHECK(u.v, 8'h63);
      `CHECK(u.v[i], 1'b1);
      `CHECK(u.v[7], 1'b0);
      u.v[5 -: 2] <= 2'b01;
      #1 `CHECK(u.v_copy, 8'h53);

      // Out-of-range writes do nothing
      i = 20;
      u.v[i] = 1'b1;
      `CHECK(u.v, 8'h53);
      `CHECK(u.v[i], 1'bx);

      // Width conversion in both directions
      u.v = 32'h1ff;
      `CHECK(u.v, 8'hff);
      wide = u.v;
      `CHECK(wide, 32'hff);
      n = u.v + 1;
      `CHECK(n, 256);

      // Concatenation mixing VHDL and Verilog targets
      {u.b, bb, u.v[1:0]} = 4'b1010;
      `CHECK(u.b, 1'b1);
      `CHECK(bb, 1'b0);
      `CHECK(u.v[1:0], 2'b10);

      // A rising index keeps the bit order
      u.up = 4'b1001;
      `CHECK(u.up[0], 1'b1);
      `CHECK(u.up[1], 1'b0);
      `CHECK(u.up, 4'b1001);

      // Bounds that depend on a generic
      u.g = 6'b110011;
      `CHECK(u.g, 6'b110011);

      // Ports
      top_in = 1;
      #1 `CHECK(u.i_port, 1'b1);
      `CHECK(u.o_port, 4'b1010);

      // Continuous assignment drives through the resolution function
      `CHECK(u.res, 1'bz);
      d = 1'b1;
      #1 `CHECK(u.res, 1'b1);
      d = 1'b0;
      #1 `CHECK(u.res, 1'b0);
      d = 1'bz;
      #1 `CHECK(u.res, 1'bz);
      dv = 4'b0001;
      #1 `CHECK(u.vres, 4'bz01z);

      // Now in conflict with the VHDL driver
      d = 1'b0;
      u.go = 1'b1;
      #1 `CHECK(u.res, 1'bx);
      d = 1'b1;
      #1 `CHECK(u.res, 1'b1);

      // Waiting on a VHDL signal
      @(posedge u.clk);
      `CHECK(u.clk, 1'b1);
      @(negedge u.clk);
      `CHECK(u.clk, 1'b0);
      @(u.clk);
      `CHECK(u.clk, 1'b1);

      // A process that writes a signal with a VHDL driver only holds until
      // that driver next changes
      u.driven = 1'b1;
      `CHECK(u.driven, 1'b1);
      #100 `CHECK(u.driven, 1'b0);

      $display("PASSED");
      $finish;
   end
endmodule
