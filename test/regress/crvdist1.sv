// Distributions of randomize() that are right in every engine: weights of a
// dist, an ordered pair, solve before, and a randc cycle
import crvdist::*;

class dist_item;
   rand bit [7:0] x;
   constraint c { x dist { 0 := 1, [1:3] := 1, [4:7] :/ 4, 200 := 8 }; }
endclass

class order_item;
   rand bit s;
   rand bit [7:0] d;
   constraint c { s -> d == 0; solve s before d; }
endclass

class chain_item;
   rand bit [3:0] a, b, c;
   constraint k { a < b; b < c; solve a before b; solve b before c; }
endclass

class cyc_item;
   randc bit [3:0] v;
   rand  bit [3:0] other;
   constraint c { other != v; }
endclass

module crvdist1;
   dist_item  di;
   order_item oi;
   chain_item ci;
   cyc_item   yi;
   hist       h;
   int        r, bad;
   bit        used[16];
   real       wd[];

   initial begin
      bad = 0;

      // 0, 1, 2 and 3 have weight 1 each, 4 to 7 share 4, 200 has 8
      di = new;
      h = new(6);
      repeat (6000) begin
         r = di.randomize();
         if (!r) $fatal(1, "dist randomize failed");
         case (di.x)
            0, 1, 2, 3: h.add(di.x);
            4, 5, 6, 7: h.add(4);
            200: h.add(5);
            default: h.add(-1);
         endcase
      end
      wd = '{1, 1, 1, 1, 4, 8};
      if (!h.check("dist weights", wd)) bad++;

      // s is a fair coin when solved first, though only one d goes with s=1
      oi = new;
      h = new(2);
      repeat (4000) begin
         r = oi.randomize();
         if (!r) $fatal(1, "order randomize failed");
         h.add(oi.s);
      end
      if (!h.check_uniform("s before d", 2)) bad++;

      // a is uniform over 0..13 when solved before b and c
      ci = new;
      h = new(14);
      repeat (14000) begin
         r = ci.randomize();
         if (!r) $fatal(1, "chain randomize failed");
         h.add(ci.a);
      end
      if (!h.check_uniform("a before b before c", 14)) bad++;

      // every value of a randc once before any repeats
      yi = new;
      repeat (3) begin
         foreach (used[i]) used[i] = 0;
         repeat (16) begin
            r = yi.randomize();
            if (!r) $fatal(1, "cycle randomize failed");
            if (used[yi.v]) begin
               $display("FAIL: randc value %0d repeated", yi.v);
               bad++;
            end
            used[yi.v] = 1;
         end
      end

      if (bad != 0)
         $fatal(1, "%0d checks failed", bad);
      $display("crvdist1 done");
      $finish;
   end
endmodule
