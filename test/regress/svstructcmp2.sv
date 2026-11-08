// Equality of unpacked structures and tagged unions with x and NaN members
// IEEE 1800-2017 11.4.5 (== gives x when no member is known to differ but
// some member compares as x; != is the logical negation of ==), 11.2.2
// (aggregate equality is member by member), 11.9 (tagged unions compare
// tags first), 6.12 (a NaN is unequal to every value including itself)
module svstructcmp2;
  typedef struct { int a; real r; } s_t;
  typedef union tagged { void N; logic [3:0] L; real R; } u_t;
  s_t a, b;
  u_t u1, u2;
  real z = 0.0, n;
  real ra[2], qa[2];
  logic x;
  int errs;
  task automatic ck(string tag, logic got, logic exp);
    if (got !== exp) begin $display("FAILED %s: got %b expected %b", tag, got, exp); errs++; end
  endtask
  initial begin
    n = z / z;
    ck("nan scalar ==", n == n, 0);
    ck("nan scalar !=", n != n, 1);
    a = '{1, n}; b = a;
    ck("nan member ==", a == b, 0);
    ck("nan member !=", a != b, 1);
    ra[0] = n; ra[1] = 1.0; qa = ra;
    ck("nan array ==", ra == qa, 0);
    ck("nan array !=", ra != qa, 1);
    a.r = 1.0; b.r = 1.0;
    ck("real member ==", a == b, 1);
    ck("real member !=", a != b, 0);
    u1 = tagged L 4'b10x1; u2 = tagged L 4'b10x1;
    ck("union x ==", u1 == u2, 1'bx);
    ck("union x !=", u1 != u2, 1'bx);
    u2 = tagged L 4'b00x1;
    ck("union x+diff ==", u1 == u2, 0);
    ck("union x+diff !=", u1 != u2, 1);
    u2 = tagged N;
    ck("union tag ==", u1 == u2, 0);
    ck("union tag !=", u1 != u2, 1);
    u1 = tagged N;
    ck("union void ==", u1 == u2, 1);
    u1 = tagged R n; u2 = tagged R n;
    ck("union nan ==", u1 == u2, 0);
    ck("union nan !=", u1 != u2, 1);
    x = (a == b);
    ck("assign", x, 1);
    if (errs == 0) $display("PASSED");
  end
endmodule
