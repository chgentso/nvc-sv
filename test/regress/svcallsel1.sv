// Expected values: IEEE 1800-2017 7.2 (struct members), 7.4 (array/queue indexing),
// 7.10 (queues), 8.5 (class methods), 6.16 (strings), 11.5.1 (bit/part select),
// 13.4 (function return values), 21.2.1.
// The 1800-2017 grammar has no select directly on a function call, so every
// function result is first stored in a temporary and the temporary is selected.
module svcallsel1;
  typedef struct { int x; bit [3:0] y; } st_t;
  typedef st_t sa_t[2];
  typedef int q_t[$];
  typedef int arr_t[3];
  typedef int mat_t[2][2];
  typedef string sq_t[$];
  typedef struct { st_t in; int arr[2]; } outer_t;
  int cnt;
  int errors = 0;
  class C;
    int v;
    function new(int v); this.v = v; endfunction
    function int get(); return v; endfunction
  endclass
  typedef C cq_t[$];
  function st_t mk(int v); st_t s; cnt++; s.x = v; s.y = v + 1; return s; endfunction
  function sa_t mka(); sa_t a; cnt++; a[0].x = 10; a[1].x = 20; a[1].y = 5; return a; endfunction
  function q_t fq(); q_t r; cnt++; r = '{4, 5, 6}; return r; endfunction
  function arr_t fa(); arr_t a; a = '{7, 8, 9}; return a; endfunction
  function mat_t fm(); mat_t m; m[0][0] = 1; m[0][1] = 2; m[1][0] = 3; m[1][1] = 4; return m; endfunction
  function sq_t fs(); sq_t r; r.push_back("zero"); r.push_back("one"); return r; endfunction
  function cq_t fc(); cq_t r; C a, b; a = new(11); b = new(22); r.push_back(a); r.push_back(b); return r; endfunction
  function outer_t fo(); outer_t o; o.in.x = 1; o.in.y = 2; o.arr[0] = 3; o.arr[1] = 4; return o; endfunction
  function bit [7:0] fv(); return 8'hA5; endfunction

  task automatic chk(input longint got, input longint exp, input string what);
    if (got !== exp) begin $error("%s = %0d, expected %0d", what, got, exp); errors++; end
  endtask

  int i, r;
  string s;
  st_t t1, t2;
  q_t tq;
  sa_t ta;
  arr_t tr;
  mat_t tm;
  sq_t tsq;
  cq_t tc;
  outer_t to;
  bit [7:0] tv;
  initial begin
    i = 2;
    t1 = mk(3);
    chk(t1.x, 3, "mk(3).x");
    chk(t1.y, 4, "mk(3).y");
    t2 = mk(4); tq = fq();
    r = t2.x + tq[i] * 2;
    chk(r, 16, "r");
    chk(cnt, 3, "cnt after mk, mk, fq");
    ta = mka();
    chk(ta[1].x, 20, "mka()[1].x");
    chk(ta[1].y, 5, "mka()[1].y");
    chk(ta[0].x, 10, "mka()[0].x");
    chk(cnt, 4, "cnt after mka");
    tr = fa();
    chk(tr[0], 7, "fa()[0]");
    chk(tr[i], 9, "fa()[2]");
    tm = fm();
    chk(tm[1][0], 3, "fm()[1][0]");
    chk(tm[0][1], 2, "fm()[0][1]");
    tsq = fs();
    s = tsq[1];
    if (s != "one") begin $error("fs()[1] = %s", s); errors++; end
    tc = fc();
    chk(tc[1].get(), 22, "fc()[1].get()");
    chk(tc[0].v, 11, "fc()[0].v");
    to = fo();
    chk(to.in.y, 2, "fo().in.y");
    chk(to.arr[1], 4, "fo().arr[1]");
    chk(to.in.x, 1, "fo().in.x");
    tv = fv();
    if (tv[7:4] !== 4'b1010) begin $error("fv()[7:4] = %b", tv[7:4]); errors++; end
    if (tv[0] !== 1'b1) begin $error("fv()[0] = %b", tv[0]); errors++; end
    tq = fq();
    if (tq[0] != 4) begin $error("fq()[0] = %0d, expected 4", tq[0]); errors++; end
    chk(cnt, 5, "cnt at end");
    if (errors == 0) $display("PASSED");
  end
endmodule
