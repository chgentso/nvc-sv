module crv05;
  class C;
    rand bit a;
    rand bit [3:0] b;
    rand bit [3:0] c;
    rand bit [1:0] mode;
    rand bit [7:0] len;
    rand bit [7:0] x, y;
    constraint k1 { a -> b == 4'd5; }
    constraint k2 { (!a) -> b != 5; }
    constraint k3 { if (mode == 0) len == 1;
                    else if (mode == 1) len inside {[2:4]};
                    else { if (a) len == 100; else len inside {[200:210]}; } }
    constraint k4 { (b > 8 || c > 8) && !(b > 8 && c > 8); }
    constraint k5 { x < 50 || y < 50; x > 10 && y > 10; }
    constraint k6 { (mode == 3) -> (c == 0 || c == 1); }
  endclass
  C o = new();
  int errors = 0;
  int i, na, n3;
  initial begin
    for (i = 0; i < 2000; i++) begin
      if (o.randomize() != 1) begin errors++; $display("rand fail"); end
      else begin
        if (o.a) na++;
        if (o.mode == 3) n3++;
        if (o.a && o.b != 5) begin errors++; $display("k1"); end
        if (!o.a && o.b == 5) begin errors++; $display("k2"); end
        if (o.mode == 0 && o.len != 1) begin errors++; $display("m0"); end
        if (o.mode == 1 && !(o.len >= 2 && o.len <= 4)) begin errors++; $display("m1"); end
        if (o.mode >= 2) begin
          if (o.a && o.len != 100) begin errors++; $display("m2a"); end
          if (!o.a && !(o.len >= 200 && o.len <= 210)) begin errors++; $display("m2b"); end
        end
        if (!((o.b > 8) ^ (o.c > 8))) begin errors++; $display("k4 %0d %0d", o.b, o.c); end
        if (!(o.x < 50 || o.y < 50)) begin errors++; $display("k5"); end
        if (!(o.x > 10 && o.y > 10)) begin errors++; $display("k5b"); end
        if (o.mode == 3 && o.c > 1) begin errors++; $display("k6"); end
      end
    end
    // both values of a must appear (implication does not make it degenerate)
    if (na < 200 || na > 1800) begin errors++; $display("na=%0d", na); end
    if (n3 == 0) begin errors++; $display("mode 3 never"); end
    if (errors == 0) $display("PASSED"); else $display("FAILED errors=%0d", errors);
    $finish;
  end
endmodule
