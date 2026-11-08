module svlrm_c18b;
  // Self-checking test. Expected values: IEEE 1800-2017 18.5.1 (constraint blocks, unique 18.5.5),
  // 18.5.3 (implication ->), 18.5.4 (if-else constraints), 18.5.4 (dist: values restricted to the
  // set, weights := per value and :/ per range; 4 carries 8 of 12 weight units), 18.5.8 (foreach
  // constraints on dynamic-array size and elements), 18.5.10 (solve...before does not change the
  // solution space), 18.5.13 (soft constraints are dropped when they conflict with hard ones),
  // 18.6.1 (randomize() returns 1 on success). Constraint expressions use the 32-bit expression
  // rules of 11.6 (v + w == 10 does not wrap at 8 bits). The dist frequency check uses a wide
  // tolerance (expected 133 of 200 for a == 4); no exact random values are compared.
  class C;
    rand bit [3:0] a, b, c;
    rand bit [3:0] arr[];
    rand bit [7:0] da[4];
    rand bit en;
    constraint cimp { en -> a > 8; }
    constraint cife { if (b > 8) c < 4; else c > 10; }
    constraint cdist { a dist { 0 := 1, [1:3] := 1, 4 :/ 8 }; }
    constraint csize { arr.size() inside {[2:4]}; foreach (arr[i]) arr[i] < i + 5; }
    constraint cuniq { unique { da }; }
    constraint csb { solve en before a; }
  endclass
  class D; rand bit [3:0] x, y; constraint s { soft x == 5; soft y inside {[1:3]}; } endclass
  class E; rand bit [7:0] v; rand bit [7:0] w; constraint r { v + w == 10; v inside {[0:20]}; w > v; } endclass
  class F; rand int s; constraint c { s inside {-5, 0, 5}; } endclass
  C o; int n; int bad; int cnt4;
  int errs;
  function automatic void ck(bit ok, string m);
    if (!ok) begin errs++; $error("%s", m); end
  endfunction
  initial begin
    o = new;
    repeat (200) begin
      if (!o.randomize()) bad++;
      if (o.en && o.a <= 8) bad++;
      if (o.b > 8 && o.c >= 4) bad++;
      if (o.b <= 8 && o.c <= 10) bad++;
      if (o.arr.size() < 2 || o.arr.size() > 4) bad++;
      foreach (o.arr[i]) if (o.arr[i] >= i + 5) bad++;
      foreach (o.da[i]) foreach (o.da[j]) if (i != j && o.da[i] == o.da[j]) bad++;
      if (o.a > 4) bad++;                 // dist restricts a to {0,1,2,3,4}
      if (o.a == 4) cnt4++;
    end
    ck(bad == 0, "constraints satisfied");
    ck(cnt4 > 90 && cnt4 < 175, "dist weight of 4 is about 2/3");
    begin
      D d; int ok;
      d = new;
      ok = d.randomize(); ck(ok == 1 && d.x == 5 && (d.y inside {[1:3]}), "soft constraints honoured");
      ok = d.randomize() with { x == 7; }; ck(ok == 1 && d.x == 7, "hard inline overrides soft x");
      ok = d.randomize() with { y == 9; }; ck(ok == 1 && d.y == 9, "hard inline overrides soft y");
    end
    begin
      E e; int ok, badv;
      e = new; badv = 0;
      repeat (50) begin ok = e.randomize(); if (!ok || e.v + e.w != 10 || e.w <= e.v) badv++; end
      ck(badv == 0, "v + w == 10 and w > v");
    end
    begin
      F f; int cnt[int]; automatic int ok_all = 1;
      f = new;
      cnt[-5] = 0; cnt[0] = 0; cnt[5] = 0;
      repeat (100) begin if (!f.randomize()) ok_all = 0; cnt[f.s]++; end
      ck(ok_all && cnt.num() == 3 && cnt[-5] > 0 && cnt[0] > 0 && cnt[5] > 0 && cnt[-5] + cnt[0] + cnt[5] == 100,
         "s only takes the three values, all of them occur");
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
