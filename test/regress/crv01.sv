module crv01;
  class C;
    rand int a;
    rand bit [7:0] b;
    rand logic [15:0] c;
    randc bit [3:0] r;
  endclass
  C o = new();
  int errors = 0;
  int ndiff_a, ndiff_b;
  int seen [16];
  int pa, pb;
  int cyc, i, j, cnt, ok;
  initial begin
    pa = 0; pb = 0;
    for (i = 0; i < 160; i++) begin
      if (o.randomize() != 1) errors++;
      if (o.a != pa) ndiff_a++;
      if (o.b != pb) ndiff_b++;
      pa = o.a; pb = o.b;
    end
    if (ndiff_a < 150) begin errors++; $display("a rarely changes %0d", ndiff_a); end
    if (ndiff_b < 120) begin errors++; $display("b rarely changes %0d", ndiff_b); end
    for (cyc = 0; cyc < 20; cyc++) begin
      for (j = 0; j < 16; j++) seen[j] = 0;
      for (j = 0; j < 16; j++) begin
        if (o.randomize() != 1) errors++;
        seen[o.r]++;
      end
      cnt = 0;
      for (j = 0; j < 16; j++) if (seen[j] == 1) cnt++;
      if (cnt != 16) begin errors++; $display("cycle %0d not permutation", cyc); end
    end
    if (errors == 0) $display("PASSED"); else $display("FAILED errors=%0d", errors);
    $finish;
  end
endmodule
