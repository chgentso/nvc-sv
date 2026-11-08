// Expected values: IEEE 1800-2017 21.2.1 (display format specifications: %x %d
// %b %o %s %t, default and minimum field widths), 20.4.2 ($timeformat),
// 20.15 ($random, $dist_uniform: deterministic sequence per seed, the seed
// variable is updated in place, results lie in the requested range,
// start == end returns start), 18.13 ($urandom, $urandom_range).
// Only format cases with a fully defined result are compared (default-width
// hex/octal/binary, %0 minimal forms, default and minimum width decimal,
// right-justified %s).  Explicit field widths smaller/larger than the
// default for %x/%b/%o and the exact numbers produced by the random
// functions are not compared; random results are checked by range,
// reproducibility (the same seed gives the same sequence) and the seed
// update rule.
`timescale 1ns/1ps
int useed = 0;

module vlog72;
   int zero = 0, v = 'h2a, neg = -5;
   logic [11:0] w = 12'h0ab;
   logic [7:0] xv = 8'b1x0z_0101;
   string s;
   int seed = 7;
   real rr;
   int errs;

   task automatic ck(string got, string exp, string what);
      if (got != exp) begin $error("%s: got [%s] expected [%s]", what, got, exp); errs++; end
   endtask
   task automatic ckb(bit c, string m); if (!c) begin $error("%s", m); errs++; end endtask

   task automatic stat(output int first, output int second);
      static int lseed = 3;
      first = $dist_uniform(lseed, 0, 1000);
      second = lseed;
   endtask

   initial begin
      int r1, r2;
      string t;
      ck($sformatf("%x", zero), "00000000", "%x zero");
      ck($sformatf("%0x", zero), "0", "%0x zero");
      ck($sformatf("%x", v), "0000002a", "%x 2a");
      ck($sformatf("%0x", v), "2a", "%0x 2a");
      ck($sformatf("%x", w), "0ab", "%x 12-bit");
      ck($sformatf("%0x", w), "ab", "%0x 12-bit");
      ck($sformatf("%x", neg), "fffffffb", "%x -5");
      ck($sformatf("%d", v), "         42", "%d 42");
      ck($sformatf("%0d", v), "42", "%0d 42");
      ck($sformatf("%3d", v), " 42", "%3d 42");
      ck($sformatf("%d", neg), "         -5", "%d -5");
      ck($sformatf("%0d", neg), "-5", "%0d -5");
      ck($sformatf("%3d", neg), " -5", "%3d -5");
      ck($sformatf("%b", w), "000010101011", "%b 12-bit");
      ck($sformatf("%0b", w), "10101011", "%0b 12-bit");
      ck($sformatf("%o", v), "00000000052", "%o 42");
      ck($sformatf("%0o", v), "52", "%0o 42");
      ck($sformatf("%b", xv), "1x0z0101", "%b with x and z");
      ck($sformatf("%0b", xv), "1x0z0101", "%0b with x and z");
      ck($sformatf("%s", "ab"), "ab", "%s");
      ck($sformatf("%10s", "ab"), "        ab", "%10s");
      ck($sformatf("%0s", "ab"), "ab", "%0s");
      $sformat(s, "+%0x: ", zero);
      ck(s, "+0: ", "$sformat");
      #1.5;
      // default $timeformat: units are the smallest precision (ps here), so
      // 1.5 ns is 1500 and $time (rounded to ns) 2 ns is 2000
      $timeformat(-9, 1, "", 0);
      ck($sformatf("%0t", $realtime), "1.5", "%0t of $realtime, ns");
      ck($sformatf("%0t", $time), "2.0", "%0t of $time, ns");
      $timeformat(-6, 4, " us", 12);
      ck($sformatf("%t", $realtime), "   0.0015 us", "%t of $realtime, us, width 12");
      ck($sformatf("%0t", $realtime), "0.0015 us", "%0t of $realtime, us");
      $timeformat(-12, 0, "", 0);
      ck($sformatf("%0t", $realtime), "1500", "%0t of $realtime, ps");
      // random numbers: sequences are reproducible from the seed
      begin
         int sa, sb, a1, a2, b1, b2;
         sa = 7; sb = 7;
         a1 = $random(sa); a2 = $random(sa);
         b1 = $random(sb); b2 = $random(sb);
         ckb(a1 == b1 && a2 == b2, "$random(seed) not reproducible for equal seeds");
         ckb(sa == sb, "seeds diverged for equal histories");
         ckb(sa != 7, "$random did not update the seed");
         sa = 8; b1 = $random(sa);
         ckb(b1 != a1 || sa != sb, "different seeds gave identical state");
      end
      begin
         int s1, s2, d1, d2, d3, d4, n;
         s1 = 11; s2 = 11;
         d1 = $dist_uniform(s1, 0, 1000); d2 = $dist_uniform(s2, 0, 1000);
         ckb(d1 == d2 && s1 == s2, "$dist_uniform not reproducible");
         ckb(s1 != 11, "$dist_uniform did not update the seed");
         for (int k = 0; k < 200; k++) begin
            n = $dist_uniform(s1, -5, 5);
            ckb(n >= -5 && n <= 5, $sformatf("$dist_uniform(-5,5) = %0d out of range", n));
            n = $dist_uniform(s1, 0, 1000);
            ckb(n >= 0 && n <= 1000, $sformatf("$dist_uniform(0,1000) = %0d out of range", n));
         end
         n = $dist_uniform(seed, 5, 5);
         ckb(n == 5, $sformatf("$dist_uniform(5,5) = %0d", n));
         ckb(($random(seed) % 10) > -10 && ($random(seed) % 10) < 10, "$random % 10 out of range");
      end
      // The seed is updated wherever the variable lives
      begin
         automatic int u0 = useed, l1, l2;
         r1 = $dist_uniform(useed, 0, 1000);
         ckb(useed != u0, "$unit-scope seed not updated");
         ckb(r1 >= 0 && r1 <= 1000, "$dist_uniform with $unit seed out of range");
         stat(r1, r2);
         ckb(r2 != 3, "task-static seed not updated");
         l1 = r2;
         stat(r1, r2);
         ckb(r2 != l1, "task-static seed did not advance on the second call");
         // restart from the same seed value reproduces the call
         begin automatic int sc = 3, f; f = $dist_uniform(sc, 0, 1000); ckb(sc == l1, "first stat() result differs from a plain call with seed 3"); end
         rr = real'($dist_uniform(useed, 0, 1000)) / 1000.0;
         ckb(rr >= 0.0 && rr <= 1.0, "scaled value out of range");
      end
      r1 = $urandom;
      r2 = $urandom_range(5, 3);
      ckb(r2 >= 3 && r2 <= 5, $sformatf("$urandom_range(5,3) = %0d", r2));
      for (int k = 0; k < 100; k++) begin
         r2 = $urandom_range(9);
         ckb(r2 >= 0 && r2 <= 9, $sformatf("$urandom_range(9) = %0d", r2));
      end
      if (errs == 0) $display("PASSED");
      $finish;
   end
endmodule
