// An unpacked array variable in a subprogram initialised from another array
typedef struct { int f[3]; } sa_t;
module vlogparrinit2;
  int g[3] = '{1,2,3};
  sa_t s = '{f: '{7,8,9}};
  function automatic int sum(int x[3]);
    int t[3] = x;
    x[0] = 100;
    return t[0] + t[1] + t[2];
  endfunction
  function automatic int sum2(int x[3]);
    int u[3];
    u = x;
    return u[0] + u[1] + u[2];
  endfunction
  function automatic int loc();
    int a[3] = '{4,5,6};
    int b[3] = a;
    a[0] = 0;
    return b[0] + b[1] + b[2];
  endfunction
  function automatic int fld(sa_t q);
    int b[3] = q.f;
    return b[0] + b[1] + b[2];
  endfunction
  function automatic int gl();
    int b[3] = g;
    g[0] = 9;
    return b[0] + b[1] + b[2];
  endfunction
  function automatic int mixed();
    real x[2] = '{1.5, 2.5};
    real r[2] = x;
    x[0] = 0.0;
    return int'(r[0] + r[1]);
  endfunction
  function automatic int m2(int x[2][2]);
    int r[2][2] = x;
    x[0][0] = 0;
    return r[0][0] + r[0][1] + r[1][0] + r[1][1];
  endfunction
  initial begin
    automatic int y[3] = '{10,20,30};
    automatic real q[2] = '{1.5, 2.5};
    automatic int mm[2][2] = '{'{1,2},'{3,4}};
    $display("%0d", sum(y));
    $display("%0d", sum2(y));
    $display("%0d", loc());
    $display("%0d", fld(s));
    $display("%0d", gl());
    $display("%0d", mixed());
    $display("%0d", m2(mm));
  end
endmodule
