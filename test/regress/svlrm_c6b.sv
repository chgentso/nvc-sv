module svlrm_c6b;
  // Self-checking test. Expected values: IEEE 1800-2017 6.19 (enumerations),
  // 6.19.4 (numerical expressions), 6.19.5 (enum methods: first, last, next, prev, num, name),
  // 6.24.1 (casting), 11.4.13 (inside), 12.5 (case), 21.2.1.7 (%p prints enum label).
  typedef enum {RED, GREEN=5, BLUE} color_e;                 // RED=0 GREEN=5 BLUE=6
  typedef enum logic [1:0] {A=0, B=1, C=2, D=3} ab_e;
  typedef enum int {X1=10, X2, X3=20, X4} xe;                // 10 11 20 21
  typedef enum bit [2:0] {S0, S1, S2 = 3'd5, S3} se;         // 0 1 5 6
  color_e c; ab_e ab; xe x; se s;
  int errs;
  function automatic void ck(bit ok, string m);
    if (!ok) begin errs++; $error("%s", m); end
  endfunction
  function automatic bit has(string s, string sub);
    for (int i = 0; i + sub.len() <= s.len(); i++)
      if (s.substr(i, i + sub.len() - 1) == sub) return 1;
    return 0;
  endfunction
  initial begin
    c = RED;       ck(c.name() == "RED" && c == 0, "RED");
    c = c.next();  ck(c.name() == "GREEN" && c == 5, "next GREEN");
    c = c.next();  ck(c.name() == "BLUE" && c == 6, "next BLUE");
    c = c.next();  ck(c.name() == "RED" && c == 0, "next wraps to RED");
    c = c.prev();  ck(c.name() == "BLUE" && c == 6, "prev wraps to BLUE");
    ck(c.num() == 3, "num");
    ck(c.first() == RED && c.last() == BLUE, "first/last");
    c = c.first(); ck(c.name() == "RED", "first");
    c = c.last();  ck(c.name() == "BLUE", "last");
    ck(c.next(2) == GREEN && c.prev(2) == RED, "next(2)/prev(2) from BLUE");
    ab = B; ck(ab.name() == "B" && ab === 2'b01, "ab B");
    ab = ab.next(3); ck(ab.name() == "A", "B next(3) = A");
    ab = ab.prev();  ck(ab.name() == "D", "A prev = D");
    x = X3; ck(x.name() == "X3" && x == 20, "X3");
    x = x.next; ck(x.name() == "X4" && x == 21, "X4");
    x = x.next; ck(x.name() == "X1" && x == 10, "X4 next wraps to X1");
    x = x.prev(3); ck(x.name() == "X2" && x == 11, "X1 prev(3) = X2");
    ck(x.num() == 4, "xe num");
    s = S2; ck(s.name() == "S2" && s == 5, "S2");
    s = s.next(); ck(s.name() == "S3", "S3");
    s = s.next(); ck(s.name() == "S0", "S3 next wraps to S0");
    c = color_e'(1); ck(c.name() == "" && c == 1, "non-member value has empty name");
    c = color_e'(5); ck(c.name() == "GREEN", "cast 5 is GREEN");
    ck(c + 1 == 6, "enum in arithmetic uses its base value");
    ck((ab == 2'd1) === 1'b0, "ab(D) != 1");
    ck((RED < BLUE) == 1, "RED < BLUE");
    ck($bits(c) == 32 && $bits(ab) == 2, "$bits");
    ck(has($sformatf("%p", c), "GREEN"), "%p shows enum label");
    ck(has($sformatf("%p", ab), "D"), "%p shows enum label D");
    ck((c inside {GREEN, BLUE}) == 1, "inside");
    case (c) RED: ck(0, "case RED"); GREEN: ; BLUE: ck(0, "case BLUE"); default: ck(0, "case default"); endcase
    if (errs == 0) $display("PASSED");
  end
endmodule
