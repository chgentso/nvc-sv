// Variables in static subroutines and blocks are static unless declared
// otherwise; class methods and subroutines of a module automatic are automatic
// (but variables of begin-end blocks in procedures stay static).
// Expected values: IEEE 1800-2017 6.21, 13.3.1, 13.4.1, 8.6, 8.10, 3.4, 9.3.1
timeunit 1ns; timeprecision 1ns;
function int uf();
   int x;
   x++;
   return x;
endfunction
package vlog73_pkg;
   function int pf();
      int x;
      x++;
      return x;
   endfunction
endpackage
import vlog73_pkg::*;
module vlog73_ma;
   task t(output int r);
      int x;
      x++;
      r = x;
   endtask
endmodule
module automatic vlog73_mb;
   int errs;
   task chk(string w, int got, int exp);
      if (got !== exp) begin errs++; $error("%s: got %0d expected %0d", w, got, exp); end
   endtask
   task t(output int r);
      int x;
      x++;
      r = x;
   endtask
   task static ts(output int r);
      int x;
      x++;
      r = x;
   endtask
   initial begin
      int r;
      #1;
      t(r); chk("mb task 1", r, 1);
      t(r); chk("mb task 2", r, 1);
      ts(r); chk("mb static task 1", r, 1);
      ts(r); chk("mb static task 2", r, 2);
      r = 0;
      repeat (2) begin
         int y;
         y++;
         r += y;   // static block variable: 1 + 2
      end
      chk("mb block", r, 3);
      begin
         int cnt;
         repeat (2) begin
            int y2;
            y2++;
            cnt += y2;
         end
         chk("mb block sum", cnt, 3);
      end
   end
endmodule
module vlog73;
   parameter P = cl(16), P8 = cl(256);
   function integer cl(input integer v);
      integer i, r;
      r = 0;
      for (i = v - 1; i > 0; i = i >> 1)
        r = r + 1;
      return r;
   endfunction
   int errs;
   task chk(string w, int got, int exp);
      if (got !== exp) begin errs++; $error("%s: got %0d expected %0d", w, got, exp); end
   endtask
   task t1(output int r, output int rz);
      int x;
      begin
         int z;
         z++;
         rz = z;
      end
      x++;
      r = x;
   endtask
   task automatic t2(output int r, output int rz);
      int x;
      begin
         int z;
         z++;
         rz = z;
      end
      x++;
      r = x;
   endtask
   task static t3(int a, output int rx, output int rw);
      int x;
      automatic int w;
      x += a;
      w += a;
      rx = x; rw = w;
   endtask
   function int f1();
      int x;
      x++;
      return x;
   endfunction
   class c;
      task t(output int r);
         int x;
         x++;
         r = x;
      endtask
      static task st(output int r);
         int x;
         x++;
         r = x;
      endtask
      function int fm();
         int x;
         x++;
         return x;
      endfunction
   endclass
   vlog73_ma a1 (), a2 ();
   vlog73_mb b1 ();
   integer n = 0;
   int aw_last;
   always @(posedge n[0]) begin
      int aw;
      aw++;
      aw_last = aw;
   end
   initial begin
      c o;
      int r, rz, r2, rx, rw;
      chk("P", P, 4); chk("P8", P8, 8);
      o = new();
      t1(r, rz); chk("t1 x 1", r, 1); chk("t1 z 1", rz, 1);
      t1(r, rz); chk("t1 x 2", r, 2); chk("t1 z 2", rz, 2);
      t2(r, rz); chk("t2 x 1", r, 1); chk("t2 z 1", rz, 1);
      t2(r, rz); chk("t2 x 1b", r, 1); chk("t2 z 1b", rz, 1);
      t3(1, rx, rw); chk("t3 x 1", rx, 1); chk("t3 w 1", rw, 1);
      t3(2, rx, rw); chk("t3 x 2", rx, 3); chk("t3 w 2", rw, 2);
      r = f1(); chk("f1 1", r, 1);
      r = f1(); chk("f1 2", r, 2);
      o.t(r); chk("c t 1", r, 1);
      o.t(r); chk("c t 2", r, 1);
      o.st(r); chk("c st 1", r, 1);
      o.st(r); chk("c st 2", r, 1);
      r = o.fm(); chk("fm 1", r, 1);
      r = o.fm(); chk("fm 2", r, 1);
      r = uf(); chk("uf 1", r, 1);
      r = uf(); chk("uf 2", r, 2);
      r = pf(); chk("pf 1", r, 1);
      r = pf(); chk("pf 2", r, 2);
      a1.t(r); chk("a1 t 1", r, 1);
      a1.t(r); chk("a1 t 2", r, 2);
      a2.t(r); chk("a2 t 1", r, 1);
      r2 = 0;
      repeat (2) begin
         int y;
         y++;
         r2 += y;      // static: 1 + 2
      end
      chk("repeat block", r2, 3);
      r2 = 0;
      for (int i = 0; i < 2; i++) begin
         int q;
         q += i + 1;
         r2 = r2 * 10 + q;   // static: q = 1 then 3
      end
      chk("for block", r2, 13);
      fork
         begin
            int fv;
            fv++;
            r = fv;
         end
         begin
            int fv;
            fv += 5;
            rz = fv;
         end
      join
      chk("fork1", r, 1); chk("fork2", rz, 5);
      #1; n = 1; #1; n = 0; #1; n = 1; #1;
      #5;
      chk("always aw", aw_last, 2);
      chk("mb errors", b1.errs, 0);
      if (errs == 0) $display("PASSED");
      $finish;
   end
endmodule
