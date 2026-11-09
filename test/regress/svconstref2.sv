// A const ref argument of a type which is not a vector (string, queue,
// dynamic array, class handle) is read through its reference (IEEE
// 1800-2017 13.5.2).

module svconstref2;
   class C;
      int v = 7;
   endclass

   function automatic int slen(const ref string s);
      if (s.len() > 2)
         return s.len();
      return -1;
   endfunction

   function automatic int qsum(const ref int q[$]);
      int n = 0;
      foreach (q[i]) n += q[i];
      return n;
   endfunction

   function automatic int dsize(const ref int d[]);
      return d.size();
   endfunction

   function automatic int cval(const ref C c);
      if (c != null)
         return c.v;
      return -1;
   endfunction

   initial begin
      string s = "hello";
      int q[$] = '{1, 2, 3};
      int d[] = new[5];
      C c = new;

      if (slen(s) != 5) $fatal(1, "slen");
      s = "ab";
      if (slen(s) != -1) $fatal(1, "slen2");
      if (qsum(q) != 6) $fatal(1, "qsum");
      if (dsize(d) != 5) $fatal(1, "dsize");
      if (cval(c) != 7) $fatal(1, "cval");
      c = null;
      if (cval(c) != -1) $fatal(1, "cval null");
      $display("PASSED");
   end
endmodule
