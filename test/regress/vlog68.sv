// Increment and decrement in expressions, fixed size array typedefs,
// comparing arrays, optional method parentheses and $sformat
module vlog68;
   typedef byte mac_t[6];
   typedef byte bytes_t[];

   class pkt;
      static const mac_t dummy = '{1, 2, 3, 4, 5, 6};
      mac_t src, dst;
      byte  payload[];
      int   count;

      function new();
         src = dummy;
      endfunction

      task bump();
         #1 count++;
      endtask
   endclass

   function automatic bytes_t make(int n);
      byte v[];
      v = new[n];
      for (int i = 0; i < n; i++)
        v[i] = 10 + i;
      return v;
   endfunction

   `define CHECK(x, y) if ((x) !== (y)) begin \
      $display("FAILED %0d: %0d", y, (x)); $finish; end

   initial begin
      int a, b, n;
      byte t[], u[];
      int q[$];
      mac_t m1, m2;
      string s;
      pkt p;

      // Postfix gives the old value and prefix the new one
      a = 5;
      b = a++;
      `CHECK(b, 5);
      `CHECK(a, 6);
      b = ++a;
      `CHECK(b, 7);
      `CHECK(a, 7);
      b = a--;
      `CHECK(b, 7);
      `CHECK(a, 6);
      b = --a;
      `CHECK(b, 5);
      `CHECK(a, 5);
      a--;
      `CHECK(a, 4);
      --a;
      `CHECK(a, 3);
      ++a;
      `CHECK(a, 4);

      // Counting down in a for loop
      n = 0;
      for (int i = 3; i >= 0; i--)
        n = n * 10 + i;
      `CHECK(n, 3210);

      // Used as an index the old value selects the element
      t = new[3];
      n = 0;
      t[n++] = 7;
      t[n++] = 8;
      t[n++] = 9;
      `CHECK(n, 3);
      `CHECK(t[0], 8'd7);
      `CHECK(t[2], 8'd9);

      // Method parentheses are optional with no arguments
      `CHECK(t.size, 3);
      q.push_back(1);
      q.push_back(2);
      `CHECK(q.size, 2);
      if (t.size < 3) $display("FAILED size");

      // Resizing keeps the elements that fit
      t = new[5](t);
      `CHECK(t.size(), 5);
      `CHECK(t[1], 8'd8);
      `CHECK(t[2], 8'd9);
      `CHECK(t[3], 8'd0);
      t = new[2](t);
      `CHECK(t.size(), 2);
      `CHECK(t[1], 8'd8);

      // A fixed size typedef starts with all of its elements
      `CHECK(m1[5], 8'd0);
      m1[5] = 42;
      m2 = m1;
      m2[5] = 43;
      `CHECK(m1[5], 8'd42);
      `CHECK(m2[5], 8'd43);

      // Assigned from a pattern, an array or a function
      m1 = '{9, 8, 7, 6, 5, 4};
      `CHECK(m1[0], 8'd9);
      `CHECK(m1[5], 8'd4);
      m2 = make(6);
      `CHECK(m2[0], 8'd10);
      `CHECK(m2[5], 8'd15);

      // Comparing arrays looks at the size and every element
      t = new[2];
      u = new[2];
      `CHECK(t == u, 1'b1);
      `CHECK(t != u, 1'b0);
      u[1] = 1;
      `CHECK(t == u, 1'b0);
      `CHECK(t != u, 1'b1);
      u = new[3];
      `CHECK(t == u, 1'b0);
      `CHECK(t === u, 1'b0);
      m1 = m2;
      if (m1 != m2 || m2 != m1) $display("FAILED mac");

      // Inside a class
      p = new;
      `CHECK(p.src[0], 8'd1);
      `CHECK(p.src[5], 8'd6);
      `CHECK(p.dst[5], 8'd0);
      `CHECK(p.src == p.dummy, 1'b1);
      p.payload = make(4);
      `CHECK(p.payload.size, 4);
      `CHECK(p.payload[p.count++], 8'd10);
      `CHECK(p.count, 1);

      // A task with no parentheses may still block
      p.bump;
      `CHECK($time, 1);
      `CHECK(p.count, 2);

      // $sformat assigns its result and can use the old value
      $sformat(s, "%0d-%s", 12, "ab");
      if (s != "12-ab") $display("FAILED sformat '%s'", s);
      $sformat(s, "%s+%0d", s, 3);
      if (s != "12-ab+3") $display("FAILED sformat '%s'", s);

      $display("PASSED");
      $finish;
   end
endmodule
