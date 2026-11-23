// Array reductions with a with clause over arrays whose size is solved
class C;
  rand bit [7:0] a[];
  rand bit [7:0] b[];
  rand bit [7:0] c[];
  rand bit [7:0] d[];
  constraint sz { a.size() inside {[2:4]}; b.size() inside {[1:3]}; c.size() == 3;
                  d.size() inside {[2:3]}; }
  constraint el { foreach (a[i]) a[i] inside {[1:5]};
                  foreach (d[i]) d[i] inside {[1:9]}; }
  constraint p1 { a.product() with (int'(item) + 1) == 24; }
  constraint p2 { b.xor() with (item) == 8'h0a; }
  constraint p3 { c.or() == 8'h0f; c.and() == 8'h01; }
  constraint p4 { d.sum() with (int'(item) * (item.index + 1)) == 20; }
endclass

module crvreduce1;
  initial begin
    C o = new;
    for (int t = 0; t < 40; t++) begin
      int p, x, orv, andv, s;
      if (!o.randomize()) $fatal(1, "randomize failed");
      p = 1;
      foreach (o.a[i]) p *= o.a[i] + 1;
      if (p != 24 || o.a.size() < 2 || o.a.size() > 4)
        $fatal(1, "product %0d size %0d", p, o.a.size());
      x = 0;
      foreach (o.b[i]) x ^= o.b[i];
      if (x != 8'h0a) $fatal(1, "xor %0d", x);
      orv = 0; andv = 255;
      foreach (o.c[i]) begin orv |= o.c[i]; andv &= o.c[i]; end
      if (orv != 15 || andv != 1) $fatal(1, "or/and %0d %0d", orv, andv);
      s = 0;
      foreach (o.d[i]) s += int'(o.d[i]) * (i + 1);
      if (s != 20) $fatal(1, "weighted sum %0d", s);
    end
    $display("PASSED");
  end
endmodule
