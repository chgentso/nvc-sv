// A signed value narrowed or widened must not change the value used later
// Expected values: IEEE 1800-2017 11.8.1 (signed operand extension), 6.24.1 (casting), 7.10 (queues), 8.7 (constructors)
// A byte of -56 widened to int is -56 (32'hffffffc8) in every context.
module svsign1;
   int err = 0;
   function automatic void chk(bit ok, string m);
      if (!ok) begin $error("%s", m); err++; end
   endfunction

   task automatic ta();
      byte b = -56;
      int k = b;
      chk(k == -56 && k == 32'hffffffc8, "task");
   endtask

   function automatic int fa(byte b);
      int k = b;
      return k == -56;
   endfunction

   class C;
      byte b = -56;
      int k;
      function new();
         k = b;
      endfunction
   endclass

   byte q[$];
   C c = new;
   int mod = 0;

   initial begin
      static byte b = -56;
      static int k = b;
      automatic byte ab = -56;
      automatic int ak = ab;
      automatic int ak2;
      chk(k == -56 && k == 32'hffffffc8, "static");
      chk(ak == -56 && ak == 32'hffffffc8 && ak === 32'b11111111111111111111111111001000, "auto");
      ak2 = ab;
      chk(ak2 == -56, "auto2");
      ta();
      chk(fa(-56) == 1, "func");
      chk(c.k == -56 && c.k == 32'hffffffc8, "class");
      q.push_back(-56);
      ak2 = q[0];
      chk(ak2 == -56, "queue");
      ak2 = int'(ab);
      chk(ak2 == -56, "cast");
      mod = ab + 0;
      chk(mod == -56 && ab == -56, "expr");
      chk(ab < 0 && ab == -56, "compare");
      if (err == 0) $display("PASSED");
   end
endmodule
