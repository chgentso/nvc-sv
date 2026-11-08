// Enumeration methods and deleting an element of a queue
typedef enum { RED, GREEN = 4, BLUE } color_t;
typedef enum logic [1:0] { A, B, C } abc_t;

module svenum1;
   `define CHECK(x, y) if ((x) !== (y)) begin \
      $display("FAILED line %0d: %0d !== %0d", `__LINE__, (x), (y)); \
      $finish; end

   color_t c;
   abc_t v;
   int q[$] = { 10, 20, 30, 40 };
   string s;

   initial begin
      c = RED;
      `CHECK(c.first(), RED);
      `CHECK(c.last(), BLUE);
      `CHECK(c.num(), 3);
      `CHECK(c.next(), GREEN);
      `CHECK(c.prev(), BLUE);   // Wraps round
      c = c.next();
      `CHECK(c, GREEN);
      `CHECK(c.next(2), RED);
      c = BLUE;
      `CHECK(c.next(), RED);
      `CHECK(c.prev(2), RED);
      s = c.name();
      `CHECK(s, "BLUE");
      `CHECK(c.name() == "BLUE", 1);
      c = GREEN;
      `CHECK(c.name(), "GREEN");

      v = C;
      `CHECK(v.next(), A);
      `CHECK(v.prev(), B);
      `CHECK(v.name() == "C", 1);

      q.delete(1);
      `CHECK(q.size(), 3);
      `CHECK(q[0], 10);
      `CHECK(q[1], 30);
      q.delete(7);
      q.delete(-1);
      `CHECK(q.size(), 3);
      q.delete(2);
      `CHECK(q.size(), 2);
      `CHECK(q[1], 30);
      q.delete();
      `CHECK(q.size(), 0);
      $display("PASSED");
   end
endmodule
