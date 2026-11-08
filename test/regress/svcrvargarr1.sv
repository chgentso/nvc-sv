// Self-checking test of std::randomize with constraints that use queue and fixed-array arguments with
// default values (function default arguments), `inside {array}`, and if-constraints on queue size.
// Expected values: IEEE 1800-2017 18.12 (std::randomize with { constraints }), 18.5.4 (inside with an
// array: set membership), 18.5.6 (if constraints), 13.5.3 (default argument values), 7.10.2 (size()).
// Only invariants are checked: the result is in the constraint set, and over 40 unconstrained-by-arguments
// draws from {A,B,C,D} more than one distinct value appears (failure probability 4*(1/4)^40).
typedef enum bit [3:0] {A, B, C, D, E} nm_t;
module svcrvargarr1;
  int fails = 0;
  function automatic nm_t pick(nm_t inc[$] = {}, nm_t exc[$] = {}, int fixed[3] = '{1, 2, 3});
    nm_t name;
    int v;
    if (!std::randomize(name) with {
          name inside {A, B, C, D};
          if (inc.size() > 0) { name inside {inc}; }
          if (exc.size() > 0) { !(name inside {exc}); }
        }) begin fails++; $error("randomize(name) failed"); end
    if (!std::randomize(v) with { v inside {fixed}; }) begin fails++; $error("randomize(v) failed"); end
    if (v < 1 || v > 3) begin fails++; $error("v = %0d outside fixed", v); end
    return name;
  endfunction
  initial begin
    int cnt[nm_t];
    automatic nm_t nil[$] = {};
    automatic nm_t inc[$] = {B, C};
    automatic nm_t exc[$] = {A, B, C};
    repeat (40) begin
      nm_t r;
      r = pick(inc, nil);
      if (!(r inside {B, C})) begin fails++; $error("inc: got %s", r.name()); end
      r = pick(nil, exc);
      if (r != D) begin fails++; $error("exc: got %s", r.name()); end
      r = pick();
      if (!(r inside {A, B, C, D})) begin fails++; $error("default: got %s", r.name()); end
      if (!cnt.exists(r)) cnt[r] = 0;
      cnt[r]++;
    end
    if (cnt.size() <= 1) begin fails++; $error("no variety in 40 draws"); end
    if (fails == 0) $display("PASSED");
  end
endmodule
