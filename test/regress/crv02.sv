module crv02;
  class C;
    rand bit [7:0] a, b, c;
    rand int x;
    rand int s, t;
    rand int unsigned w;
    rand bit [7:0] p, q;
    constraint c_rel { a < 100; a >= 10; b > 20; b <= 30; c != 5; c != 6; c < 8; }
    constraint c_cmp { p < q; q != p + 1; q <= 50; }
    constraint c_sum { a + b == c + 100; }  // widths 32 in expression context? see check
    constraint c_sgn { s > -50; s < 50; t == s * 2 - 10; x > 1000000 && x < 1000010; }
    constraint c_w { w == 123456789; }
  endclass
  class D;
    rand bit [7:0] a, b, c;
    rand int s;
    constraint k1 { a + b == c; }
    constraint k2 { a > 3; b > 3; c < 200; }
    constraint k3 { s >= -5; s <= -3; }
    constraint k4 { a == b; }
  endclass
  C o = new();
  D d = new();
  int errors = 0;
  int i;
  int pa, pb;
  initial begin
    // C: a+b==c+100 with 8-bit: a in[10,99], b in[21,30], c in {0..7}\{5,6}: a+b >= 31, c+100 in 100..107 -> a+b in 100..107
    for (i = 0; i < 300; i++) begin
      if (o.randomize() != 1) begin errors++; $display("rand fail %0d", i); end
      else begin
        if (!(o.a >= 10 && o.a < 100)) begin errors++; $display("a=%0d", o.a); end
        if (!(o.b > 20 && o.b <= 30)) begin errors++; $display("b=%0d", o.b); end
        if (o.c == 5 || o.c == 6 || o.c >= 8) begin errors++; $display("c=%0d", o.c); end
        if (!(o.p < o.q)) begin errors++; $display("p<q"); end
        if (o.q == o.p + 1) begin errors++; $display("q!=p+1"); end
        if (o.q > 50) begin errors++; $display("q"); end
        if (o.a + o.b != o.c + 100) begin errors++; $display("sum %0d %0d %0d", o.a, o.b, o.c); end
        if (!(o.s > -50 && o.s < 50)) begin errors++; $display("s=%0d", o.s); end
        if (o.t != o.s * 2 - 10) begin errors++; $display("t"); end
        if (!(o.x > 1000000 && o.x < 1000010)) begin errors++; $display("x=%0d", o.x); end
        if (o.w != 123456789) begin errors++; $display("w"); end
      end
    end
    for (i = 0; i < 300; i++) begin
      if (d.randomize() != 1) begin errors++; $display("D rand fail"); end
      else begin
        if (d.a + d.b != d.c) begin errors++; $display("D sum"); end
        if (d.a != d.b) errors++;
        if (!(d.a > 3 && d.c < 200)) errors++;
        if (!(d.s >= -5 && d.s <= -3)) begin errors++; $display("D s=%0d", d.s); end
      end
    end
    if (errors == 0) $display("PASSED"); else $display("FAILED errors=%0d", errors);
    $finish;
  end
endmodule
