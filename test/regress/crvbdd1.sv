// Constraints on the bits of values that no sampling of the variables
// alone finds and that the search needs a rewrite of the slices for: the
// bdd stage solves them and draws the solutions uniformly
import crvdist::*;

// Each bit of x and y is one of (0,0), (0,1) and (1,0): x has k bits set in
// C(8,k) 2^(8-k) of the 6561 solutions
class and_item;
   rand bit [7:0] x, y;
   constraint c { (x & y) == 0; }
endclass

// The bits of x and y are fixed but those that decide which of them has the
// bit: 16 solutions
class andor_item;
   rand bit [15:0] x, y;
   constraint c { (x & y) == 16'h0f00; (x | y) == 16'hff00; }
endclass

// A mask on 32 bits and a count over an array: the bits left free are free
class mask_item;
   rand bit [31:0] x;
   rand bit [7:0] f[8];
   constraint c { (x & 32'h0ff00f00) == 32'h05500a00;
                  (x | 32'hffff0000) == 32'hffff1a34;
                  f.or() == 8'hf0; }
endclass

// A slice across the two 64 bit chunks of a 108 bit value
class slice_item;
   rand bit [107:0] v;
   constraint c { v[71:54] == 18'h1234; v[3:0] != 4'hf; }
endclass

module crvbdd1;
   and_item   ai;
   andor_item oi;
   mask_item  mi;
   slice_item si;
   hist       h;
   int        r, bad, lo, hi;
   real       wa[];

   initial begin
      bad = 0;

      ai = new;
      h = new(9);
      repeat (20000) begin
         r = ai.randomize();
         if (!r || (ai.x & ai.y) != 0) $fatal(1, "x & y failed");
         h.add($countones(ai.x));
      end
      wa = '{256, 1024, 1792, 1792, 1120, 448, 112, 16, 1};
      if (!h.check("x & y == 0: bits set in x", wa)) bad++;

      oi = new;
      h = new(16);
      repeat (4000) begin
         r = oi.randomize();
         if (!r || (oi.x & oi.y) != 16'h0f00 || (oi.x | oi.y) != 16'hff00)
            $fatal(1, "and/or failed");
         h.add(oi.x[15:12]);
      end
      if (!h.check_uniform("x & y, x | y over 4 free bits", 16)) bad++;

      mi = new;
      h = new(2);
      lo = 0;
      hi = 0;
      repeat (200) begin
         r = mi.randomize();
         if (!r || (mi.x & 32'h0ff00f00) != 32'h05500a00
             || (mi.x | 32'hffff0000) != 32'hffff1a34)
            $fatal(1, "mask failed");
         else begin
            int s;
            s = 0;
            foreach (mi.f[i]) s |= mi.f[i];
            if (s != 8'hf0) $fatal(1, "or of the array %h", s);
            lo |= mi.x[31:28];
            hi |= ~mi.x[31:28] & 4'hf;
         end
      end
      if (lo != 4'hf || hi != 4'hf) begin
         $error("free bits of x did not take both values: %h %h", lo, hi);
         bad++;
      end

      si = new;
      lo = 0;
      hi = 0;
      repeat (200) begin
         r = si.randomize();
         if (!r || si.v[71:54] != 18'h1234 || si.v[3:0] == 4'hf)
            $fatal(1, "slice failed");
         lo |= si.v[107];
         hi |= !si.v[107];
         lo |= si.v[0] ? 2 : 0;
      end
      if (lo != 3 || hi != 1) begin
         $error("bits of v: %0d %0d", lo, hi);
         bad++;
      end

      if (bad != 0)
         $fatal(1, "%0d checks failed", bad);
      $display("crvbdd1 done");
      $finish;
   end
endmodule
