// Queue and dynamic array concatenation and array method edge cases
module svqueue1;
   `define CHECK(x, y) if ((x) !== (y)) begin \
      $display("FAILED %0d: %0d", y, (x)); $finish; end

   int ia[] = { 4, 5, 3, 1 };
   int q[$];
   int r[$];
   string s[] = { "hello", "sad", "world" };
   byte b[] = { 1, 3, 5, 7 };

   initial begin
      `CHECK(ia.size(), 4);
      `CHECK(ia[2], 3);
      ia.sort;
      `CHECK(ia[0], 1);
      `CHECK(ia[3], 5);

      q = { 1, 2, 3, 4 };
      q = { q[0:1], 10, q[2:$] };
      `CHECK(q.size(), 5);
      `CHECK(q[2], 10);
      `CHECK(q[4], 4);
      q = { q, 4 };
      q = { 0, q };
      `CHECK(q.size(), 7);
      `CHECK(q[0], 0);
      `CHECK(q[6], 4);
      q = { q, q };
      `CHECK(q.size(), 14);
      r = { };
      `CHECK(r.size(), 0);

      s.reverse;
      `CHECK(s[0], "world");
      `CHECK(s[2], "hello");

      `CHECK(b.and, 1);
      `CHECK(b.or, 7);
      `CHECK(b.xor, 0);
      `CHECK(b.sum, 16);
      $display("PASSED");
   end
endmodule
