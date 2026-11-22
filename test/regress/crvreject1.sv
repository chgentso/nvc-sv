// The distributions the language asks for, for constraints that the
// rejection stage answers: uniform over the solutions, a dist by the weight
// of each value among those that can be extended, solve before, and a
// variable that an equality defines
import crvdist::*;

class lt_item;
   rand bit [3:0] x, y;
   constraint c { x < y; }
endclass

// a is chosen first among the values that b can follow
class order_item;
   rand bit [3:0] a, b;
   constraint c { a < b; solve a before b; }
endclass

class impl_item;
   rand bit s;
   rand bit [7:0] d;
   constraint c { s -> d == 0; }
endclass

class mode_item;
   rand bit mode;
   rand bit [3:0] len;
   constraint c {
      if (mode == 0) len < 10;
      else len inside {[5:7]};
   }
endclass

// y <= 2x leaves 1, 3, 5 and 7 values of y for x = 0 to 3: x is chosen by
// its weight alone
class dist_item;
   rand bit [1:0] x;
   rand bit [2:0] y;
   constraint c {
      x dist { 0 := 1, [1:2] :/ 4, 3 := 1 };
      y <= 3'(x) * 3'd2;
   }
endclass

// A 32 bit total that only the sum defines: nearly every sample would be
// rejected without computing it
class total_item;
   rand bit [31:0] total;
   rand bit [2:0] a, b;
   constraint c { total == 32'(a) + 32'(b); a != b; }
endclass

module crvreject1;
   lt_item    li;
   order_item oi;
   impl_item  ii;
   mode_item  mi;
   dist_item  di;
   total_item ti;
   hist       h;
   int        r, bad;
   real       w2[], wd[];

   initial begin
      bad = 0;

      li = new;
      h = new(120);
      repeat (20000) begin
         r = li.randomize();
         if (!r || !(li.x < li.y)) $fatal(1, "x < y failed");
         h.add(li.x * 15 - li.x * (li.x - 1) / 2 + (li.y - li.x - 1));
      end
      if (!h.check_uniform("x<y over 4 bits", 120)) bad++;

      oi = new;
      h = new(15);
      repeat (15000) begin
         r = oi.randomize();
         if (!r || !(oi.a < oi.b)) $fatal(1, "order failed");
         h.add(oi.a);
      end
      if (!h.check_uniform("a before b", 15)) bad++;

      ii = new;
      h = new(2);
      repeat (20000) begin
         r = ii.randomize();
         if (!r || (ii.s && ii.d != 0)) $fatal(1, "implication failed");
         h.add(ii.s);
      end
      w2 = '{256, 1};
      if (!h.check("s -> d==0 without an order", w2)) bad++;

      mi = new;
      h = new(2);
      repeat (20000) begin
         r = mi.randomize();
         if (!r) $fatal(1, "modes failed");
         h.add(mi.mode);
      end
      w2 = '{10, 3};
      if (!h.check("if/else modes", w2)) bad++;

      // 1:2:2:1 by weight whatever the number of values of y
      di = new;
      h = new(4);
      repeat (12000) begin
         r = di.randomize();
         if (!r || di.y > 2 * di.x) $fatal(1, "dist failed");
         h.add(di.x);
      end
      wd = '{1, 2, 2, 1};
      if (!h.check("dist weights by value among those that extend", wd)) bad++;

      // Each of the 56 pairs equally, the total always the sum
      ti = new;
      h = new(56);
      repeat (20000) begin
         r = ti.randomize();
         if (!r || ti.total != ti.a + ti.b || ti.a == ti.b)
            $fatal(1, "total failed");
         h.add(ti.a * 7 + (ti.b > ti.a ? ti.b - 1 : ti.b));
      end
      if (!h.check_uniform("total == a + b", 56)) bad++;

      if (bad != 0)
         $fatal(1, "%0d checks failed", bad);
      $display("crvreject1 done");
      $finish;
   end
endmodule
