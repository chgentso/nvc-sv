// The result of a relational, equality or logical operator is a 1-bit
// integral value whatever the operand types, and it is extended in a
// wider context like any other 1-bit value
// IEEE 1800-2017 11.4.4 (relational), 11.4.5 (equality), 11.4.7 (logical),
// 11.6.1 (expression bit lengths), 7.6 / 11.2.2 (aggregate equality)
module svreltype1;
  localparam bit P = (1.5 > 1.0);
  localparam int Q = (2.0 == 2.0) + (1.0 < 0.5) * 4 + 8;
  typedef struct { int a; real b; } s_t;
  typedef union tagged { void N; int I; } u_t;
  real r = 2.0, q = 3.0;
  real ra[2] = '{1.0, 2.0}, qa[2] = '{1.0, 2.0};
  s_t s1 = '{1, 2.0}, s2 = '{1, 2.0};
  u_t u1, u2;
  string t1 = "ab", t2 = "ab";
  int d1[$] = {1}, d2[$] = {1};
  wire w = (r < q);
  bit x;
  logic y;
  int k, err = 0;
  task automatic chk(bit ok, string m);
    if (!ok) begin $display("FAILED %s", m); err++; end
  endtask
  initial begin
    u1 = tagged I 5; u2 = tagged I 5;
    #1;
    chk(P === 1'b1 && Q === 9, "constant");
    chk((r < q) === 1'b1 && (r > q) !== 1'b1, "real relational");
    chk((r == 2.0) === 1'b1 && (r != q) + 1 === 2, "real equality");
    chk((ra == qa) === 1'b1 && (ra != qa) === 1'b0, "real array equality");
    x = (s1 == s2);
    chk(x === 1'b1, "struct equality to bit");
    chk((s1 == s2) == 1, "struct equality in equality");
    if (s1 == s2 && 1) ; else chk(0, "struct equality in logical and");
    y = (u1 == u2);
    chk(y === 1'b1, "tagged union equality");
    chk(w === 1'b1, "continuous assignment");
    chk($bits(r < q) == 1 && $bits(s1 == s2) == 1, "$bits");
    k = (r < q) + (r > q) * 2 + ((s1 == s2) << 2) + ((s1 != s2) << 3);
    chk(k == 5, "context");
    k = ((t1 == t2) << 1) + ((d1 == d2) << 2) + ((4'b1010 ==? 4'b1x1x) << 3);
    chk(k == 14, "string, queue and wildcard context");
    k = (r && q) + (r || 0.0) * 2;
    chk(k == 3, "real logical");
    if (err == 0) $display("PASSED");
  end
endmodule
