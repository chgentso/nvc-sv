module svarrmap1;
   initial begin
      automatic int a[5] = '{1, 2, 3, 4, 5};
      automatic int q[$] = '{10, 20, 30};
      int e[$];
      int r[$];

      r = a.map() with (item * 2);
      if (r.size() != 5 || r[0] != 2 || r[4] != 10)
         $fatal(1, "fixed map failed");

      r = a.map(x) with (x + x.index);
      if (r.size() != 5 || r[0] != 1 || r[2] != 5 || r[4] != 9)
         $fatal(1, "fixed map index failed");

      r = q.map() with (item + 1);
      if (r.size() != 3 || r[0] != 11 || r[2] != 31)
         $fatal(1, "queue map failed");

      r = e.map() with (item * 2);
      if (r.size() != 0)
         $fatal(1, "empty map failed");

      $display("PASSED");
   end
endmodule
