// The inverting reduction operators in a constraint, on narrow and wide
// operands
module svrandreduce1;
   class c;
      rand bit [383:0] w;
      rand bit [15:0]  n;
      rand bit         e1, e2, e3, e4, e5, e6;

      constraint k {
         e1 == (~|w);
         e2 == (~&w);
         e3 == (~^w);
         e4 == (~|n);
         e5 == (~&n);
         e6 == (~^n);
      }

      function bit check();
         return e1 == (~|w) && e2 == (~&w) && e3 == (~^w)
            && e4 == (~|n) && e5 == (~&n) && e6 == (~^n);
      endfunction
   endclass

   initial begin
      automatic c o = new;
      int ones = 0;
      repeat (100) begin
         if (!o.randomize()) $fatal(1, "randomize failed");
         if (!o.check()) $fatal(1, "constraint violated");
         ones += o.e3 + o.e6;
      end
      if (ones == 0 || ones == 200) $fatal(1, "no variety %0d", ones);
      $display("PASSED");
   end
endmodule
