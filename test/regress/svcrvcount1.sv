// Self-checking test of $countones, $onehot and $onehot0 inside constraints, including a vector wider than
// 32 bits and an inline `with` constraint.
// Expected values: IEEE 1800-2017 18.4 and 18.5 (system functions in constraints are evaluated on the
// random variables), 20.9 ($countones, $onehot, $onehot0).  Every solution must satisfy the constraints:
// countones(a) == 1, countones(b) in {2,3}, onehot(k), countones(w) == 3; onehot0(z) && z != 0 (exactly one
// bit), countones(y) > 4.  Over 100 draws the one-hot `a` must take more than one position (failure
// probability 16 * (1/16)^100 for a uniform solver).
module svcrvcount1;
  class c;
    rand bit [15:0] a;
    rand bit [7:0] b;
    rand bit [3:0] k;
    rand bit [40:0] w;
    constraint c1 { $countones(a) == 1; }
    constraint c2 { $countones(b) inside {2, 3}; }
    constraint c3 { $onehot(k); }
    constraint c4 { $countones(w) == 3; }
  endclass
  class d;
    rand bit [5:0] z;
    rand bit [5:0] y;
    constraint c1 { $onehot0(z); z != 0; }
    constraint c2 { $countones(y) > 4; }
  endclass
  function automatic int ones(bit [63:0] v);
    int n = 0;
    for (int i = 0; i < 64; i++) n += v[i];
    return n;
  endfunction
  initial begin
    automatic c o = new;
    automatic d p = new;
    automatic int bad = 0, bad2 = 0, fails = 0;
    automatic bit [15:0] seen_a = 0;
    repeat (100) begin
      if (!o.randomize()) bad++;
      else if (ones(o.a) != 1 || (ones(o.b) != 2 && ones(o.b) != 3)
               || ones(o.k) != 1 || ones(o.w) != 3) bad++;
      seen_a |= o.a;
      if (!p.randomize()) bad2++;
      else if (ones(p.z) != 1 || ones(p.y) < 5) bad2++;
    end
    if (bad != 0 || bad2 != 0) begin fails++; $error("bad=%0d bad2=%0d", bad, bad2); end
    if (ones(seen_a) < 2) begin fails++; $error("one-hot a never changed position: %h", seen_a); end
    if (!(o.randomize() with { $countones(a) == 1 && a[3]; }) || o.a != 16'h0008) begin
      fails++; $error("inline with: a = %h", o.a);
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
