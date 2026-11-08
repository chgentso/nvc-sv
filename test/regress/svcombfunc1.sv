// Functions called from always_comb and continuous assignments
// Expected values: IEEE 1800-2017 9.2.2.2 (always_comb runs once at time zero, then on
// changes of its inputs), 10.3.2 (continuous assignment with a function call), 13.4
// (automatic functions, return), 12.7 (loops, do-while, repeat), 7.2 (packed structs)
`timescale 1ns/1ns
package p;
  typedef struct packed { logic [3:0] a; logic [3:0] b; } s_t;
  function automatic bit is_true(logic [3:0] v);
    return v == 4'h6;
  endfunction
  function automatic int count_ones(logic [7:0] v);
    int n = 0;
    int i = 0;
    while (i < 8) begin
      if (v[i]) n++;
      i++;
    end
    return n;
  endfunction
  function automatic int first_set(logic [7:0] v);
    for (int i = 0; i < 8; i++)
      if (v[i]) return i;
    return -1;
  endfunction
  function automatic s_t mk(logic [3:0] x);
    s_t r = '{a: x, b: ~x};
    return r;
  endfunction
  function automatic int rep(logic [3:0] x);
    int t = 0;
    repeat (3) t += x;
    do begin t++; end while (t < 2);
    return t;
  endfunction
endpackage
module svcombfunc1;
  logic [7:0] x = 8'h6;
  wire y = p::is_true(x[3:0]);
  bit z;
  int cnt, fs, rp;
  p::s_t d, e;
  logic [3:0] q = 3;
  int errs = 0;
  always_comb z = p::is_true(x[3:0]);
  always_comb cnt = p::count_ones(x);
  always_comb fs = p::first_set(x);
  always_comb rp = p::rep(x[3:0]);
  always_comb begin
    d = '{a: q, b: 4'd5};
    e = p::mk(q);
  end
  function automatic void chk(string m, longint got, longint exp);
    if (got !== exp) begin $error("%s: got %0d expected %0d", m, got, exp); errs++; end
  endfunction
  initial begin
    #1;
    chk("y1", y, 1); chk("z1", z, 1); chk("cnt1", cnt, 2); chk("fs1", fs, 1);
    chk("rp1", rp, 19); chk("d.a1", d.a, 3); chk("e1", e, 8'h3c); chk("d.b1", d.b, 5);
    x = 8'h81; q = 4; #1;
    chk("y2", y, 0); chk("z2", z, 0); chk("cnt2", cnt, 2); chk("fs2", fs, 0);
    chk("rp2", rp, 4); chk("d.a2", d.a, 4); chk("e2", e, 8'h4b);
    if (errs == 0) $display("PASSED");
  end
endmodule
