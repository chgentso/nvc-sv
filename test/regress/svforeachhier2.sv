// IEEE 1800-2017 12.7.3: foreach over a hierarchical array reference whose
// instance is declared later, is one of an array of instances, is made by a
// generate block or has a parameterised size
module svforeachhier2_sub #(parameter N = 3);
  int c[N] = '{default: 4};
  int m[2][N];
  int d[3:0] = '{1, 2, 3, 4};
  int q[$] = '{9, 8, 7};
  int e[];
  struct { int f[2]; } s = '{f: '{5, 6}};
  initial foreach (m[i, j]) m[i][j] = i * 10 + j;
endmodule

module svforeachhier2;
  initial begin
    #1;
    foreach (u.c[i]) $display("u.c[%0d]", i);
    foreach (ua[1].c[i]) $display("ua1.c[%0d]", i);
    foreach (ua[0].m[i, j]) $display("ua0.m[%0d][%0d]=%0d", i, j, ua[0].m[i][j]);
    foreach (g[1].gu.c[i]) $display("g1.c[%0d]", i);
    foreach (u.d[i]) $display("u.d[%0d]", i);
    foreach (u.q[i]) $display("u.q[%0d]=%0d", i, u.q[i]);
    foreach (u.e[i]) $display("empty %0d", i);
    foreach (u.s.f[i]) $display("s.f[%0d]=%0d", i, u.s.f[i]);
    foreach (u.m[1][j]) $display("m1[%0d]", j);
    foreach (u.m[, j]) $display("mj %0d", j);
    t();
    $display("%0d", fn());
  end

  task t;
    foreach (u.c[i]) $display("task c[%0d]", i);
  endtask

  function automatic int fn;
    int n = 0;
    foreach (u.m[i, j]) n += u.m[i][j];
    return n;
  endfunction

  svforeachhier2_sub u();
  svforeachhier2_sub #(2) ua[2]();
  for (genvar k = 0; k < 2; k++) begin : g
    svforeachhier2_sub #(k + 2) gu();
  end
endmodule
