// The right operand of && and || is not evaluated when the left decides
// the result, even if it selects with an index read through a null handle
module svshortcirc1;
   class tok;
      int endpoint = 1;
      int pid = 2;
   endclass

   class bfm;
      tok rx;
      bit en[4] = '{1, 0, 1, 1};
      bit [7:0] vec = 8'hf0;
      bit q[$] = '{1, 0, 1};

      function int a();
         if (rx != null && rx.pid == 2 && !en[rx.endpoint]) return 1;
         return 0;
      endfunction

      function int b();
         if (rx != null && !en[rx.endpoint]) return 1;
         return 0;
      endfunction

      function int c();
         if (rx == null || en[rx.endpoint]) return 1;
         return 0;
      endfunction

      function int d();
         if (rx != null && vec[rx.endpoint +: 2] == 2'b00) return 1;
         return 0;
      endfunction

      function int e();
         if (rx != null && q[rx.endpoint] == 0) return 1;
         return 0;
      endfunction
   endclass

   initial begin
      automatic bfm m = new;
      if (m.a() != 0 || m.b() != 0 || m.c() != 1 || m.d() != 0 || m.e() != 0)
         $fatal(1, "null handle %0d %0d %0d %0d %0d", m.a(), m.b(), m.c(),
                m.d(), m.e());

      m.rx = new;
      if (m.a() != 1 || m.b() != 1 || m.c() != 0)
         $fatal(1, "handle %0d %0d %0d", m.a(), m.b(), m.c());
      m.rx.endpoint = 4;
      if (m.d() != 0) $fatal(1, "d");
      $display("PASSED");
   end
endmodule
