// Self-checking: null handles, handle comparison and truth value, handles in containers.
// Expected values: IEEE 1800-2017 8.4 (handles; default value null), 8.12/11.4.5 (handle ==, != null), 12.4
// (a handle in a condition is false only for null), 7.5, 7.8, 7.10 (containers of handles)
// ($bits of a handle is left out: its width is not fixed by the LRM)
`define CHK(g, e) if ((g) !== (e)) begin errs++; $error("check failed at line %0d", `__LINE__); end
module svlrm_c8j;
  int errs;
  class P; int x; function new(int v = 0); x = v; endfunction endclass
  P a, b, c;
  P q[$]; P aa[string]; P dyn[];
  int t;
  initial begin
    `CHK(a == null, 1)
    a = new(1); b = a; c = new(1);
    `CHK(a == b, 1) `CHK(a == c, 0) `CHK(a != c, 1)
    t = 0; if (a) t = 1; `CHK(t, 1)
    t = 0; if (!c) t = 1; `CHK(t, 0)
    c = null; t = 0; if (!c) t = 1; `CHK(t, 1)
    `CHK(a && b, 1)
    `CHK(a && c, 0)
    q.push_back(a); q.push_back(c); q.push_back(b);
    `CHK(q[0] == q[2], 1) `CHK(q[1] == null, 1) `CHK(q.size(), 3)
    aa["a"] = a; aa["n"] = null;
    `CHK(aa["a"] == a, 1) `CHK(aa["n"] == null, 1)
    `CHK(aa.exists("q"), 0)
    dyn = new[2]; `CHK(dyn[0] == null, 1) `CHK(dyn[1] == null, 1)
    dyn[1] = a; a.x = 42; `CHK(dyn[1].x, 42)
    begin P tt; tt = q[0]; tt.x = 7; `CHK(a.x, 7) end
    q.delete(); `CHK(q.size(), 0)
    begin P arr [2]; arr[0] = null; `CHK(arr[0] == null, 1) arr[0] = new(3); `CHK(arr[0].x, 3) `CHK(arr[1] == null, 1) end
    if (errs == 0) $display("PASSED");
  end
endmodule
