// Assignment expressions, concatenation selects, casts, streaming
// concatenation and intra-assignment event controls
module svexpr1;
   `define CHECK(x, y) if ((x) !== (y)) begin \
      $display("FAILED %0d: %0d", y, (x)); $finish; end

   int a, b, c, d, e;
   bit [3:0] n;
   bit [7:0] hi = 8'b10101100;
   bit [7:0] lo = 8'b01010011;
   logic x;
   integer s;
   struct packed {logic [7:0] a; logic [7:0] b; logic [15:0] c;} st;

   bit [127:0] wide;
   bit [95:0] mid;
   logic [11:0] w = 12'hABC;
   logic [11:0] r;
   logic [15:0] r16;
   bit [7:0] p, q;
   bit [31:0] src = 32'h11223344;
   int ia = 1, ib = 2, ic = 3;
   int i, v1, v2, v3;
   reg clk = 0;

   always #5 clk = ~clk;

   initial begin
      c = a;
      b = (a += 1);
      `CHECK(b, c + 1);
      b = (a -= 3);
      `CHECK(b, -2);
      a = (b = (c = 5));
      `CHECK(a, 5);
      `CHECK(b, 5);
      `CHECK(c, 5);
      e = b;
      d = ((b += (a += 1) + 1));
      `CHECK(b, e + 7);
      `CHECK(d, e + 7);

      n = {hi, lo}[9:6];
      `CHECK(n, 4'b0001);
      x = {hi, lo}[15];
      `CHECK(x, 1'b1);
      `CHECK({hi, lo}[3:0], 4'b0011);
      `CHECK({hi, lo}[4 +: 4], 4'b0101);
      `CHECK({hi, lo}[12 -: 3], 3'b011);

      st = 32'hdeadbeef;
      s = integer'(st);
      `CHECK(s, 32'hdeadbeef);
      `CHECK(bit'(5), 1'b1);
      `CHECK(logic'(6), 1'b0);
      `CHECK(integer'(-3), -3);
      `CHECK(int'(7), 7);

      wide = {<< 32 {ia, ib, ic}};
      `CHECK(wide, 128'h00000003_00000002_00000001_00000000);
      wide = {>> 32 {ia, ib, ic}};
      `CHECK(wide, 128'h00000001_00000002_00000003_00000000);
      mid = {<< 32 {ia, ib, ic}};
      `CHECK(mid, 96'h00000003_00000002_00000001);
      r = {<< 8 {w}};
      `CHECK(r, 12'hbca);
      r = {<< 4 {w}};
      `CHECK(r, 12'hcba);
      r16 = {<< 8 {w}};
      `CHECK(r16, 16'hbca0);
      r16 = {>> {w}};
      `CHECK(r16, 16'habc0);
      `CHECK({<< byte {16'hAABB}}, 16'hbbaa);
      `CHECK({<< 12 {16'hABCD}}, 16'hbcda);
      {<< 8 {p, q}} = 16'h1234;
      `CHECK(p, 8'h34);
      `CHECK(q, 8'h12);
      {>> 8 {p, q}} = 32'h11223344;
      `CHECK(p, 8'h11);
      `CHECK(q, 8'h22);
      {<< 4 {p, q}} = 16'h1234;
      `CHECK(p, 8'h43);
      `CHECK(q, 8'h21);
      {<< 8 {ia, ib}} = {<< 8 {src, 32'h55667788}};
      `CHECK(ia, 32'h11223344);
      `CHECK(ib, 32'h55667788);

      // Right hand side is sampled before the delay or event
      #1;
      v1 = 1;
      fork
         begin v2 = #5 v1; end
         begin #2 v1 = 2; end
      join
      `CHECK(v2, 1);

      #20;
      i = 2;
      v1 = 7;
      fork
         begin v3 = repeat (i) @(posedge clk) v1; end
         begin #1 v1 = 8; end
      join
      `CHECK(v3, 7);
      i = 0;
      v3 = repeat (i) @(posedge clk) 9;
      `CHECK(v3, 9);
      i = -3;
      v3 = repeat (i) @(posedge clk) 10;
      `CHECK(v3, 10);
      v3 = @(posedge clk) 11;
      `CHECK(v3, 11);
      $display("PASSED");
      $finish;
   end
endmodule
