// Equality of unpacked structures compares their members
// Expected values: IEEE 1800-2017 11.2.2 (aggregate expressions: unpacked structures
// may be compared with == and != member by member), 11.4.5 (== gives 1'bx when the result
// is ambiguous because of x/z bits and no member differs for sure; != is the logical
// negation of ==), 11.4.6 (=== / !== compare x/z exactly and give 0 or 1), 7.4.6 (a read
// outside a queue returns the default value, x for 4-state members). Case equality is
// not applied to a struct with a real member (Table 11-1), so the members are compared.

module svstructcmp1;
  typedef struct { logic valid; logic signed [15:0] data; } fd_t;
  typedef struct { bit [1:0] z; } in_t;
  typedef struct { logic [3:0] a; real r; in_t in; } s_t;
  fd_t q[$], e, o, a, b;
  s_t p, r;
  int errs;
  task automatic ck(string tag, logic got, logic exp);
    if (got !== exp) begin $error("%s: got %b expected %b", tag, got, exp); errs++; end
  endtask
  function automatic logic seq(s_t m, s_t n); return m == n; endfunction
  function automatic logic sne(s_t m, s_t n); return m != n; endfunction
  initial begin
    q.push_back('{1'b1, 16'sd5});
    e = q[3]; o = q[4];
    // both default (x) values: ambiguous
    ck("e==o dflt", e == o, 1'bx);
    ck("e!=o dflt", e != o, 1'bx);
    ck("e===o dflt", e === o, 1'b1);
    a = '{1'b1, 16'sd5}; b = '{1'b1, 16'sd6};
    ck("a==b diff", a == b, 1'b0);
    ck("a!=b diff", a != b, 1'b1);
    ck("a===b diff", a === b, 1'b0);
    b = a;
    ck("a==b same", a == b, 1'b1);
    ck("a!=b same", a != b, 1'b0);
    ck("a===b same", a === b, 1'b1);
    e = '{1'bx, 16'sd5}; b = '{1'bx, 16'sd5};
    ck("x ==", e == b, 1'bx);
    ck("x !=", e != b, 1'bx);
    ck("x ===", e === b, 1'b1);
    ck("x !==", e !== b, 1'b0);
    // a known difference in another member wins over an x
    b = '{1'bx, 16'sd6};
    ck("x+diff ==", e == b, 1'b0);
    ck("x+diff !=", e != b, 1'b1);
    // nested struct, real member, x in a member
    p.a = 4'bx01x; p.r = 1.5; p.in.z = 2'b10; r = p;
    ck("p==r", seq(p, r), 1'bx);
    ck("p!=r", sne(p, r), 1'bx);
    ck("p.a===r.a", p.a === r.a, 1'b1);
    ck("p.in===r.in", p.in === r.in, 1'b1);
    if (!(p.r == r.r)) begin $error("real members differ"); errs++; end
    r.in.z = 2'b11;
    ck("p==r in differs", seq(p, r), 1'b0);
    ck("p!=r in differs", sne(p, r), 1'b1);
    ck("p.in!==r.in", p.in !== r.in, 1'b1);
    r = p; r.r = 2.5;
    ck("p==r real differs", seq(p, r), 1'b0);
    ck("p!=r real differs", sne(p, r), 1'b1);
    if (errs == 0) $display("PASSED");
  end
endmodule
