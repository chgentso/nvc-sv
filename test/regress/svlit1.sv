// Unbased unsized literals and implicit named port connections
module svlit1_sub(input [3:0] pa, input pb, output [3:0] py, output pz);
   assign py = pa + 1;
   assign pz = ~pb;
endmodule

module svlit1;
   `define CHECK(x, y) if ((x) !== (y)) begin \
      $display("FAILED %0d: %0d", y, (x)); $finish; end

   parameter int foo = '0;
   logic [15:0] a, b, c, d;
   logic [99:0] w, z;
   bit [7:0] e;

   logic [3:0] pa = 4'd5;
   logic pb = 1;
   wire [3:0] py;
   wire pz;
   wire [3:0] pq;
   svlit1_sub u1 (.*);
   svlit1_sub u2 (.pa(pa), .pb(1'b0), .py(pq), .pz());

   initial begin
      a = '0; b = '1; c = 'x; d = 'z;
      `CHECK(a, 16'h0000);
      `CHECK(b, 16'hffff);
      `CHECK(c, 16'hxxxx);
      `CHECK(d, 16'hzzzz);
      w = '1; z = 'x;
      `CHECK(w, {100{1'b1}});
      `CHECK(z, {100{1'bx}});
      e = '1;
      `CHECK(e, 8'hff);
      `CHECK(e & '1, 8'hff);
      e = 8'hf0 | '1;
      `CHECK(e, 8'hff);
      `CHECK(b == '1, 1'b1);
      `CHECK(foo, 0);
      #1;
      `CHECK(py, 4'd6);
      `CHECK(pz, 1'b0);
      `CHECK(pq, 4'd6);
      $display("PASSED");
   end
endmodule
