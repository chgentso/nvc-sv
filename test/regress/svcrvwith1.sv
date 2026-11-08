// Self-checking: array reduction methods with "with" clauses inside constraints.
// Expected values: IEEE 1800-2017 18.5 (constraints), 7.12.3 (array reduction methods sum/product/and/or/xor
// with a with-clause; item.index 7.12), 6.24.1 (int' cast).
class c1;
  rand bit [7:0] a [4];
  rand int b [3];
  rand byte c [5];
  constraint k1 { foreach (a[i]) a[i] inside {[1:9]}; a.sum() with (int'(item)) == 20; }
  constraint k2 { foreach (b[i]) b[i] inside {[-5:5]}; b.sum() with (item * item) == 14; }
  constraint k3 { foreach (c[i]) c[i] inside {[-3:3]}; c.sum() with (item + 1) == 12; }
  constraint k4 { a.product() with (int'(item)) <= 200; a.xor() with (int'(item)) != 0; }
  constraint k5 { b.sum() with (item * (item.index + 1)) == 7; }
endclass
class c2;
  rand bit [7:0] a [3];
  rand bit [7:0] b [3];
  constraint k1 { foreach (a[i]) a[i] inside {[1:20]}; foreach (b[i]) b[i] inside {[1:20]}; }
  constraint k2 { a.sum() with (int'(item)) > b.sum() with (int'(item)) + 10; }
  constraint k3 { a.or() with (int'(item)) == 8'h1F; }
  constraint k4 { b.and() with (int'(item)) inside {1, 3}; }
endclass
module svcrvwith1;
  c1 x = new; c2 y = new;
  int bad1, bad2, ok1, ok2;
  initial begin
    repeat (50) begin
      int sa, sb, sc, pa, xa, sb2, o, an, sa2, sb3;
      if (x.randomize()) begin
        ok1++;
        sa = 0; foreach (x.a[i]) sa += x.a[i];
        sb = 0; foreach (x.b[i]) sb += x.b[i] * x.b[i];
        sc = 0; foreach (x.c[i]) sc += x.c[i] + 1;
        pa = 1; foreach (x.a[i]) pa *= x.a[i];
        xa = 0; foreach (x.a[i]) xa ^= x.a[i];
        sb2 = 0; foreach (x.b[i]) sb2 += x.b[i] * (i + 1);
        if (sa != 20 || sb != 14 || sc != 12 || pa > 200 || xa == 0 || sb2 != 7) bad1++;
      end
      if (y.randomize()) begin
        ok2++;
        sa2 = 0; o = 0; foreach (y.a[i]) begin sa2 += y.a[i]; o |= y.a[i]; end
        sb3 = 0; an = 8'hFF; foreach (y.b[i]) begin sb3 += y.b[i]; an &= y.b[i]; end
        if (!(sa2 > sb3 + 10) || o != 8'h1F || !(an == 1 || an == 3)) bad2++;
      end
    end
    if (ok1 != 50 || ok2 != 50 || bad1 != 0 || bad2 != 0)
      $error("ok1=%0d bad1=%0d ok2=%0d bad2=%0d, expected 50 0 50 0", ok1, bad1, ok2, bad2);
    else $display("PASSED");
  end
endmodule
