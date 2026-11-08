// Assignment patterns, copies and comparisons of unpacked arrays
module svunpack1;
   `define CHECK(x, y) if ((x) !== (y)) begin \
      $display("FAILED %0d: %0d", y, (x)); $finish; end

   int a[3] = '{1, 2, 3};
   int b[3];
   int m[2][2] = '{'{1, 2}, '{3, 4}};
   bit [3:0] k[4] = '{default: 5};
   int s[0:3] = '{2: 9, default: 1};
   bit r[6] = '{3{1, 0}};
   bit w[6] = '{6{1}};
   int q[$];
   int sl[$];

   function automatic int f(int x);
      int c[3] = '{4, 5, 6};
      int d[3];
      d = c;
      d[1] = x;
      return d[0] + d[1] + d[2] + (c == d) + c[1];
   endfunction

   task automatic t;
      int e[2][2];
      int h[3];
      e = '{'{1, 2}, '{3, 4}};
      `CHECK(e[1][0], 3);
      h = a;
      `CHECK(h[0], 1);
      `CHECK(h[2], 3);
      a = '{7, 7, 7};
      `CHECK(a[1], 7);
      a = h;
      `CHECK(a[0], 1);
      `CHECK(h == a, 1);
      e[0] = '{5, 6};
      `CHECK(e[0][1], 6);
      `CHECK(e[1][1], 4);
   endtask

   initial begin
      b = a;
      `CHECK(b[2], 3);
      `CHECK(b == a, 1);
      b[0] = 9;
      `CHECK(b != a, 1);
      `CHECK(m[0][1], 2);
      `CHECK(m[1][0], 3);
      `CHECK(k[3], 5);
      `CHECK(s[2], 9);
      `CHECK(s[3], 1);
      `CHECK(f(10), 25);
      t;

      q = '{10, 11, 12, 13, 14, 15};
      sl = q[2:4];
      `CHECK(sl.size(), 3);
      `CHECK(sl[0], 12);
      `CHECK(sl[2], 14);
      sl = q[4:2];
      `CHECK(sl.size(), 0);
      sl = q[-2:1];
      `CHECK(sl.size(), 2);
      `CHECK(sl[0], 10);
      sl = q[3:$];
      `CHECK(sl.size(), 3);
      `CHECK(sl[2], 15);
      sl = q[2:10];
      `CHECK(sl.size(), 4);
      $display("PASSED");
   end
endmodule
