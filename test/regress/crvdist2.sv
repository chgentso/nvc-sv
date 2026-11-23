// The distribution of the search alone where it is not uniform
// over the solutions: the numbers are printed as a record, and the shell
// part of the test checks that the helper sees the bias.  Run with
// NVC_CRV_ENGINE=dfs: the exact stages are meant to be uniform here.
import crvdist::*;

class lt_item;
   rand bit [3:0] x, y;
   constraint c { x < y; }
endclass

class sum_item;
   rand bit [3:0] x, y, z;
   constraint c { (6'(x) + 6'(y) + 6'(z)) == 6'd10; }
endclass

class impl_item;
   rand bit s;
   rand bit [7:0] d;
   constraint c { s -> d == 0; }
endclass

class mode_item;
   rand bit [1:0] mode;
   rand bit [7:0] len;
   constraint c {
      if (mode == 0) len < 10;
      else if (mode == 1) len inside {[100:110]};
      else len == 255;
   }
endclass

module crvdist2;
   lt_item   li;
   sum_item  si;
   impl_item ii;
   mode_item mi;
   hist      h;
   int       r;
   real      w2[], w4[];

   initial begin
      li = new;
      h = new(120);
      repeat (20000) begin
         r = li.randomize();
         // Index of the pair among x < y
         h.add(li.x * 15 - li.x * (li.x - 1) / 2 + (li.y - li.x - 1));
      end
      h.report("x<y over 4 bits", 120);

      si = new;
      h = new(66);
      repeat (20000) begin
         r = si.randomize();
         // Pairs (x, y) with z = 10 - x - y in 0..15
         h.add(si.x * 11 - si.x * (si.x - 1) / 2 + si.y);
      end
      h.report("x+y+z==10", 66);

      ii = new;
      h = new(2);
      repeat (20000) begin
         r = ii.randomize();
         h.add(ii.s);
      end
      w2 = '{256, 1};
      h.report_weights("s -> d==0 without an order", w2);

      mi = new;
      h = new(4);
      repeat (20000) begin
         r = mi.randomize();
         h.add(mi.mode);
      end
      w4 = '{10, 11, 1, 1};
      h.report_weights("if/else modes (10:11:1:1 over the solutions)", w4);
      $finish;
   end
endmodule
