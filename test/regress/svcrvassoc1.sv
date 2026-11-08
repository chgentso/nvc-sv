// Self-checking test of randomization of associative arrays: constraints on elements by key, foreach
// over the existing keys, soft constraints, cross-array constraints, unique, std/inline `with`.
// Expected values: IEEE 1800-2017 18.4 (rand variables incl. associative arrays; only existing elements are
// randomized), 18.5.8 (foreach iterative constraints), 18.5.10 (soft constraints are dropped when they
// conflict with a hard constraint), 18.5.5 (uniqueness), 7.8 (associative arrays, num()).
// Only invariants are checked: every solution satisfies the constraints; the unconstrained bits of `a` take
// both values (more than 10 and fewer than 90 ones among 100 draws).  Left out: the value of the element
// y.m[2] (any value <= 1), whether constraints naming absent keys (m[77], m[78]) create elements, and the
// outcome of the final randomize() with such a key.
module svcrvassoc1;
  typedef enum {DIRECT, VECTORED, EXTRA} mode_t;
  class cfg;
    rand bit a[mode_t];
    rand bit [7:0] b[int];
    rand bit [3:0] c[string];
    bit [7:0] n[int];
    bit enable = 1;
    bit force1 = 0;
    constraint ca {
      a[EXTRA] == 1'b0;
      if (force1) {
        foreach (a[i]) soft a[i] == 1'b1;
      }
    }
    constraint cb {
      foreach (b[k]) {
        if (k <= 100) b[k] inside {[10:20]};
        if (k == 3) b[k] == 15;
        if (k > 100) b[k] == k - 100;
      }
      b[7] != 12;
    }
    constraint cc {
      c["y"] != 0; c["zz"] > 2;
      c["x"] == 5;
    }
    constraint cd {
      b[5] >= n[5];
    }
  endclass
  class xcfg;
    rand bit [7:0] m[int];
    rand bit [7:0] u[mode_t];
    bit [7:0] lim[int];
    bit uniq = 0;
    constraint c1 { foreach (lim[k]) m[k] <= lim[k]; }
    constraint c2 { m[77] == 0; foreach (m[i]) soft m[i] > 1; }
    constraint c3 { if (uniq) unique {u}; }
  endclass
  initial begin
    automatic cfg x = new;
    automatic xcfg y = new;
    automatic int bad = 0, ones = 0, varied, fails = 0;
    x.a[DIRECT] = 0; x.a[VECTORED] = 0; x.a[EXTRA] = 1;
    foreach (x.b[i]) ;
    x.b[3] = 0; x.b[7] = 0; x.b[150] = 0; x.b[5] = 0;
    x.c["x"] = 0; x.c["y"] = 0; x.c["zz"] = 0;
    x.n[5] = 18;
    repeat (50) begin
      if (!x.randomize()) bad++;
      if (x.a[EXTRA] != 0) bad++;
      if (x.b[3] != 15) bad++;
      if (x.b[7] == 12 || x.b[7] < 10 || x.b[7] > 20) bad++;
      if (x.b[150] != 50) bad++;
      if (x.b[5] < 18 || x.b[5] > 20) bad++;
      if (x.c["x"] != 5 || x.c["y"] == 0 || x.c["zz"] == 0) bad++;
      if (x.a.num() != 3 || x.b.num() != 4 || x.c.num() != 3) bad++;
      ones += x.a[DIRECT] + x.a[VECTORED];
    end
    varied = ones > 10 && ones < 90;
    if (bad != 0 || !varied) begin fails++; $error("phase 1: bad=%0d varied=%0d", bad, varied); end
    x.force1 = 1;
    repeat (10) begin
      void'(x.randomize());
      if (x.a[DIRECT] != 1 || x.a[VECTORED] != 1 || x.a[EXTRA] != 0) bad++;
    end
    if (bad != 0) begin fails++; $error("phase 2 (force1): bad=%0d", bad); end
    y.m[1] = 0; y.m[2] = 0; y.m[9] = 0;
    y.u[DIRECT] = 0; y.u[VECTORED] = 0; y.u[EXTRA] = 0;
    y.lim[1] = 7; y.lim[2] = 1; y.lim[9] = 3;
    bad = 0;
    repeat (20) begin
      if (!y.randomize()) bad++;
      if (y.m[1] > 7 || y.m[2] > 1 || y.m[9] > 3) bad++;
      if (y.m[1] <= 1 || y.m[9] <= 1) bad++;
    end
    if (bad != 0 || y.m[2] > 1) begin fails++; $error("cross: bad=%0d m2=%0d", bad, y.m[2]); end
    y.uniq = 1;
    bad = 0;
    repeat (20) begin
      if (!y.randomize()) bad++;
      if (y.u[DIRECT] == y.u[VECTORED] || y.u[DIRECT] == y.u[EXTRA]
          || y.u[VECTORED] == y.u[EXTRA]) bad++;
    end
    if (bad != 0) begin fails++; $error("unique: bad=%0d", bad); end
    if (y.randomize() with { m[9] == 3; m[78] == 0; } && y.m[9] != 3) begin fails++; $error("inline with: m[9] = %0d", y.m[9]); end
    if (fails == 0) $display("PASSED");
  end
endmodule
