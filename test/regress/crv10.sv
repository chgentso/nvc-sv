module crv10;
  class C;
    rand int a;
    rand int b;
    constraint ka { a inside {[1:10]}; }
    constraint kb { b inside {[100:110]}; }
  endclass
  C o = new();
  int errors = 0;
  int i, pa, pb, ndiff;
  initial begin
    // property rand_mode
    if (o.randomize() != 1) errors++;
    o.a = 5;
    o.a.rand_mode(0);
    ndiff = 0;
    for (i = 0; i < 100; i++) begin
      if (o.randomize() != 1) errors++;
      if (o.a != 5) begin errors++; $display("a changed while off"); end
      if (o.b < 100 || o.b > 110) errors++;
      if (o.b != pb) ndiff++;
      pb = o.b;
    end
    if (ndiff < 50) begin errors++; $display("b not varying"); end
    if (o.a.rand_mode() != 0) begin errors++; $display("a.rand_mode() func"); end
    if (o.b.rand_mode() != 1) begin errors++; $display("b.rand_mode() func"); end
    o.a.rand_mode(1);
    if (o.a.rand_mode() != 1) errors++;
    ndiff = 0;
    for (i = 0; i < 100; i++) begin
      if (o.randomize() != 1) errors++;
      if (o.a < 1 || o.a > 10) begin errors++; $display("a=%0d", o.a); end
      if (o.a != 5) ndiff++;
    end
    if (ndiff < 50) errors++;
    // whole object rand_mode
    o.a = 3; o.b = 105;
    o.rand_mode(0);
    for (i = 0; i < 20; i++) begin
      if (o.randomize() != 1) errors++;
      if (o.a != 3 || o.b != 105) begin errors++; $display("changed while object off"); end
    end
    if (o.a.rand_mode() != 0 || o.b.rand_mode() != 0) begin errors++; $display("obj off, props not off"); end
    o.rand_mode(1);
    if (o.randomize() != 1) errors++;
    if (o.a < 1 || o.a > 10 || o.b < 100 || o.b > 110) begin errors++; $display("after obj on"); end
    // constraint_mode on one constraint
    o.ka.constraint_mode(0);
    ndiff = 0;
    for (i = 0; i < 200; i++) begin
      if (o.randomize() != 1) errors++;
      if (o.a < 1 || o.a > 10) ndiff++;
      if (o.b < 100 || o.b > 110) begin errors++; $display("kb broken"); end
    end
    if (ndiff < 190) begin errors++; $display("ka off: a still constrained %0d", ndiff); end
    if (o.ka.constraint_mode() != 0) begin errors++; $display("ka.constraint_mode() func"); end
    if (o.kb.constraint_mode() != 1) begin errors++; $display("kb.constraint_mode() func"); end
    o.ka.constraint_mode(1);
    for (i = 0; i < 50; i++) begin
      if (o.randomize() != 1) errors++;
      if (o.a < 1 || o.a > 10) begin errors++; $display("ka re-enabled"); end
    end
    // whole-object constraint_mode
    o.constraint_mode(0);
    ndiff = 0;
    for (i = 0; i < 200; i++) begin
      if (o.randomize() != 1) errors++;
      if (o.a < 1 || o.a > 10) ndiff++;
      if (o.b < 100 || o.b > 110) ndiff++;
    end
    if (ndiff < 380) begin errors++; $display("all off: still constrained %0d", ndiff); end
    o.constraint_mode(1);
    for (i = 0; i < 50; i++) begin
      if (o.randomize() != 1) errors++;
      if (o.a < 1 || o.a > 10 || o.b < 100 || o.b > 110) begin errors++; $display("all on again"); end
    end
    // disabling a constraint can turn an unsat inline into sat
    if (o.randomize() with { a > 50; } != 0) begin errors++; $display("expected unsat"); end
    o.ka.constraint_mode(0);
    if (o.randomize() with { a > 50; } != 1) errors++;
    else if (o.a <= 50) errors++;
    if (errors == 0) $display("PASSED"); else $display("FAILED errors=%0d", errors);
    $finish;
  end
endmodule
