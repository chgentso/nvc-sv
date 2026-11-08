module svlrm_c18a;
  // Self-checking test. Expected values: IEEE 1800-2017 18.4 (rand and randc variables; randc cycles
  // through all values of its range before repeating, so 200 calls on a 2-bit randc give each value
  // exactly 50 times and every aligned group of 4 is a permutation, 18.4.2), 18.5 (constraint blocks:
  // inside, relational, % and !=), 18.6.1 (randomize() returns 1 on success; non-rand members are
  // untouched), 18.7 (inline constraints with `with`), 18.8 (rand_mode), 18.9 (constraint_mode),
  // 18.12 (std::randomize / scope randomize). Random values themselves are never compared exactly.
  class Pkt;
    rand bit [7:0] addr;
    rand bit [3:0] len;
    randc bit [1:0] mode;
    rand int unsigned v;
    bit [7:0] fixed = 8'hAA;
    constraint c1 { addr inside {[10:20]}; len > 2; len < 9; }
    constraint c2 { v < 100; v % 2 == 0; }
    constraint c3 { addr != 15; }
  endclass
  Pkt p; int ok; int hist[bit [1:0]]; int bad;
  int errs;
  int seen4[bit [1:0]];
  function automatic void ck(bit ok, string m);
    if (!ok) begin errs++; $error("%s", m); end
  endfunction
  initial begin
    p = new;
    for (int k = 0; k < 4; k++) hist[k] = 0;
    for (int n = 0; n < 200; n++) begin
      ok = p.randomize();
      if (!ok) bad++;
      if (!(p.addr inside {[10:20]}) || p.addr == 15) bad++;
      if (!(p.len > 2 && p.len < 9)) bad++;
      if (p.v >= 100 || p.v % 2) bad++;
      if (p.fixed != 8'hAA) bad++;
      hist[p.mode]++;
      if (n % 4 == 0) seen4.delete();
      seen4[p.mode] = 1;
      if (n % 4 == 3 && seen4.num() != 4) errs++;
    end
    ck(bad == 0, "constraints satisfied in 200 randomizations");
    ck(errs == 0, "randc: every aligned group of 4 is a permutation");
    ck(hist[0] == 50 && hist[1] == 50 && hist[2] == 50 && hist[3] == 50, "randc exact distribution");
    repeat (20) begin ok = p.randomize() with { addr == 12; len == 5; }; if (!ok || p.addr != 12 || p.len != 5) bad++; end
    ck(bad == 0, "inline constraints");
    p.c1.constraint_mode(0);
    p.addr.rand_mode(0);
    p.addr = 8'd99;
    repeat (10) begin ok = p.randomize(); if (!ok || p.addr != 99) bad++; end
    ck(bad == 0 && p.addr == 99, "rand_mode(0) keeps addr");
    p.addr.rand_mode(1);
    p.c3.constraint_mode(0);
    repeat (50) begin ok = p.randomize() with { addr == 15; }; if (!ok || p.addr != 15) bad++; end
    ck(bad == 0, "constraint_mode(0) allows addr == 15");
    ok = std::randomize(bad) with { bad inside {5, 6}; };
    ck(ok == 1 && (bad inside {5, 6}), "std::randomize with");
    begin int a, b; ok = randomize(a, b) with { a < b; a > 0; b < 4; };
      ck(ok == 1 && a < b && a > 0 && b < 4, "scope randomize"); end
    if (errs == 0) $display("PASSED");
  end
endmodule
