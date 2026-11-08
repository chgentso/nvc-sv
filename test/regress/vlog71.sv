// Variables declared static in a subroutine or block keep their value
int ucount = 0;

function automatic int uf();
   static int a = 5;
   a++;
   return a + ucount;
endfunction

package vlog71_pkg;
   function automatic int pf();
      static int a = 50;
      a++;
      return a;
   endfunction
endpackage

import vlog71_pkg::*;

module vlog71_sub;
   function automatic int sf();
      static int a = 500;
      a++;
      return a;
   endfunction
endmodule

module vlog71;
   vlog71_sub s1 (), s2 ();
   int a = 1000;
   int calls1 = 0;

   task t1();
      static int a = 5;
      a++;
      if (a != 5 + calls1) $display("FAILED t1 %0d", a);
   endtask

   task automatic t2();
      static int a = 5, b = 7;
      automatic int c = 5;
      a++;
      b += 2;
      c++;
      if (c != 6) $display("FAILED c %0d", c);
      $display("t2 %0d %0d", a, b);
   endtask

   function int f1();
      static int a = 5;
      a++;
      return a;
   endfunction

   class c;
      task t3(int want);
         static int a = 5;
         a++;
         if (a != want) $display("FAILED t3 %0d", a);
      endtask
   endclass

   `define CHECK(x, y) if ((x) !== (y)) begin \
      $display("FAILED %0d: %0d", y, (x)); $finish; end

   initial begin
      automatic c o = new;
      automatic c o2 = new;

      calls1 = 1;
      t1();
      calls1 = 2;
      t1();

      t2();
      t2();

      `CHECK(f1(), 6);
      `CHECK(f1(), 7);

      // Shared between all objects of the class
      o.t3(6);
      o2.t3(7);

      `CHECK(uf(), 6);
      `CHECK(uf(), 7);
      `CHECK(pf(), 51);
      `CHECK(pf(), 52);

      // One copy for each instance
      `CHECK(s1.sf(), 501);
      `CHECK(s1.sf(), 502);
      `CHECK(s2.sf(), 501);

      // Not confused with the variable in the module
      `CHECK(a, 1000);

      begin
         static int z = 3;
         z++;
         `CHECK(z, 4);
      end

      $display("PASSED");
      $finish;
   end
endmodule
