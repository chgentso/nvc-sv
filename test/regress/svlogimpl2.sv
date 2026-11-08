// Expected values: IEEE 1800-2017 11.4.7 (-> and <-> in expressions; -> is right associative),
// 11.3.2 (operator precedence: -> and <-> are below ?: and &), 16.3 (immediate assertions: pass or
// fail action block), 12.4, 18.5.6 (implication constraint: x > 4 -> y == 1)
class k;
  rand bit [3:0] x, y;
  constraint c { x > 4 -> y == 1; }
endclass
module svlogimpl2;
  logic v = 1, z = 1;
  logic [3:0] u = 0;
  int n;
  int errs = 0;
  int passed1 = 0, failed2 = 0, if3 = 0, broken = 0;
  initial begin
    for (int a = 0; a < 2; a++)
      for (int b = 0; b < 2; b++)
        for (int c = 0; c < 2; c++) begin
          if ((a -> b) !== ((!a) || b)) begin errs++; $error("a->b %0d%0d", a, b); end
          if ((a <-> b) !== (a == b)) begin errs++; $error("a<->b %0d%0d", a, b); end
          if ((a -> b -> c) !== ((!a) || (!b) || c)) begin errs++; $error("a->b->c %0d%0d%0d", a, b, c); end
          if ((a & b -> c) !== ((!(a & b)) || c)) begin errs++; $error("a&b->c %0d%0d%0d", a, b, c); end
        end
    #1;
    assert (v & z -> ~|u) passed1++; else begin errs++; $error("first assert failed"); end
    assert (v -> !z) begin errs++; $error("second assert passed"); end else failed2++;
    if (v -> z) if3++;
    // -> has lower precedence than ?: so this is v -> (z ? 5 : 6), i.e. 1 -> 5, which is 1
    n = (v -> z ? 5 : 6);
    if (n !== 1) begin errs++; $error("n=%0d", n); end
    begin
      automatic k o = new;
      repeat (20) begin
        if (!o.randomize()) begin errs++; $error("randomize failed"); end
        if (o.x > 4 && o.y != 1) broken++;
      end
      if (broken != 0) begin errs++; $error("constraint broken"); end
    end
    if (passed1 != 1 || failed2 != 1 || if3 != 1) begin errs++; $error("counts %0d %0d %0d", passed1, failed2, if3); end
    if (errs == 0) $display("PASSED");
  end
  always_comb assert (v -> z) else begin errs++; $error("always_comb assert failed"); end
endmodule
