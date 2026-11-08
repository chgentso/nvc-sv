// Unpacked array arguments of tasks and functions
// Self-checking. Expected values: IEEE 1800-2017 13.5 (argument passing: input = copy in, output = copy out,
// inout = both, ref = alias), 7.4.2/7.4.5 (unpacked arrays; array arguments correspond element by element from
// the left bound, regardless of the index ranges), 10.9.1 (array assignment patterns list elements from the
// left index), 20.7 ($size, $low).
`define CHK(tag, got, exp) if ((got) !== (exp)) begin errs++; $error("%s: got %0d expected %0d", tag, got, exp); end
module svarrarg1;
  int errs = 0;
  typedef int t3 [3];
  int b [2:0];
  int c [1:3];
  logic [7:0] l [4];
  int m [2][2];
  t3 u;
  int g0, g1, g2;       // last values seen by fun()
  int qsz, qlow;

  task automatic fun(int a [2:0]);
    g0 = a[0]; g1 = a[1]; g2 = a[2];
  endtask

  function automatic int sum(int a [2:0]);
    sum = a[0] + a[1] * 10 + a[2] * 100;
  endfunction

  function automatic int sum2(input int a [1:3]);
    a[1] = 100;
    return a[1] + a[2] + a[3];
  endfunction

  task automatic fill(output int a [1:3], input int v);
    a[1] = v; a[2] = v + 1; a[3] = v + 2;
  endtask

  task automatic inc(inout int a [2:0]);
    for (int i = 0; i < 3; i++) a[i] = a[i] + 1;
  endtask

  task automatic reft(ref int a [2:0]);
    a[1] = 77;
  endtask

  function automatic int lsum(logic [7:0] x [4]);
    return x[0] + x[1] + x[2] + x[3];
  endfunction

  function automatic int msum(int x [2][2]);
    return x[0][0] + x[0][1] * 10 + x[1][0] * 100 + x[1][1] * 1000;
  endfunction

  task automatic outer();
    int lb [2:0] = '{1, 2, 3};   // lb[2]=1, lb[1]=2, lb[0]=3
    fun(lb);
    `CHK("outer fun g0", g0, 3) `CHK("outer fun g1", g1, 2) `CHK("outer fun g2", g2, 1)
    reft(lb);
    `CHK("R0", lb[0], 3) `CHK("R1", lb[1], 77) `CHK("R2", lb[2], 1)
    inc(lb);
    `CHK("outer inc0", lb[0], 4) `CHK("outer inc1", lb[1], 78) `CHK("outer inc2", lb[2], 2)
    sum2c(lb);
    `CHK("$size", qsz, 3) `CHK("$low", qlow, 0)
  endtask

  task automatic sum2c(int x [2:0]);
    qsz = $size(x); qlow = $low(x);
  endtask

  initial begin
    b[0] = 0; b[1] = 1; b[2] = 2;
    fun(b);
    `CHK("fun(b) g0", g0, 0) `CHK("fun(b) g1", g1, 1) `CHK("fun(b) g2", g2, 2)
    `CHK("sum(b)", sum(b), 210)
    fun('{5, 6, 7});          // a[2]=5, a[1]=6, a[0]=7
    `CHK("fun(pat) g0", g0, 7) `CHK("fun(pat) g1", g1, 6) `CHK("fun(pat) g2", g2, 5)
    c = '{1, 2, 3};
    `CHK("sum2(c)", sum2(c), 105)
    `CHK("c[1] unchanged by input copy", c[1], 1)
    fill(c, 10);
    `CHK("F1", c[1], 10) `CHK("F2", c[2], 11) `CHK("F3", c[3], 12)
    inc(b);
    `CHK("I0", b[0], 1) `CHK("I1", b[1], 2) `CHK("I2", b[2], 3)
    outer();
    l = '{1, 2, 3, 4};
    `CHK("lsum", lsum(l), 10)
    m = '{'{1, 2}, '{3, 4}};
    `CHK("msum", msum(m), 4321)
    u = '{9, 8, 7};          // u[0]=9 ... maps onto a[2]=9, a[1]=8, a[0]=7
    fun(u);
    `CHK("fun(u) g0", g0, 7) `CHK("fun(u) g1", g1, 8) `CHK("fun(u) g2", g2, 9)
    `CHK("sum(u)", sum(u), 987)
    if (errs == 0) $display("PASSED");
  end
endmodule
