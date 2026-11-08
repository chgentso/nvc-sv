module crv03;
  class C;
    rand int a;
    rand bit [7:0] b;
    rand int c;
    rand bit [7:0] d;
    rand int lo, hi;
    rand int e;
    constraint k1 { a inside {1, 3, 5, [10:12], 100}; }
    constraint k2 { b inside {[20:30]}; !(b inside {[22:27]}); }
    constraint k3 { c inside {[-5:-1], [1:5]}; }
    constraint k4 { lo inside {[0:9]}; hi inside {[20:29]}; d inside {[lo:hi]}; }
    constraint k5 { !(e inside {0, 1, 2}); e inside {[0:5]}; }
  endclass
  class V;
    rand int x, y;
    rand int m;
    constraint k { y inside {[0:100]}; x inside {y, y+1}; m inside {x, 7}; }
  endclass
  C o = new();
  V v = new();
  int errors = 0;
  int i, n, hit;
  int hits [128];
  int nhit;
  initial begin
    for (i = 0; i < 500; i++) begin
      if (o.randomize() != 1) begin errors++; $display("rand fail"); end
      else begin
        if (!(o.a == 1 || o.a == 3 || o.a == 5 || (o.a >= 10 && o.a <= 12) || o.a == 100)) begin errors++; $display("a=%0d", o.a); end
        if (!((o.b >= 20 && o.b <= 21) || (o.b >= 28 && o.b <= 30))) begin errors++; $display("b=%0d", o.b); end
        if (o.c == 0 || o.c < -5 || o.c > 5) begin errors++; $display("c=%0d", o.c); end
        if (!(o.d >= o.lo && o.d <= o.hi)) begin errors++; $display("d"); end
        if (!(o.e >= 3 && o.e <= 5)) begin errors++; $display("e=%0d", o.e); end
        if (o.a >= 0 && o.a < 128) hits[o.a] = 1;
      end
      if (v.randomize() != 1) begin errors++; $display("V rand fail"); end
      else begin
        if (!(v.x == v.y || v.x == v.y + 1)) begin errors++; $display("x"); end
        if (!(v.m == v.x || v.m == 7)) begin errors++; $display("m"); end
      end
    end
    // all values of a should appear
    nhit = 0;
    for (i = 0; i < 128; i++) if (hits[i] != 0) nhit++;
    if (nhit != 7) begin errors++; $display("a coverage %0d", nhit); end
    // inside as a plain expression
    n = 5;
    hit = 0;
    if (n inside {1, 2, [4:6]}) hit++;
    if (n inside {[10:20]}) hit += 10;
    if (!(n inside {0, 9})) hit += 100;
    if (n inside {n}) hit += 1000;
    if (hit != 1101) begin errors++; $display("inside expr %0d", hit); end
    if (errors == 0) $display("PASSED"); else $display("FAILED errors=%0d", errors);
    $finish;
  end
endmodule
