// Self-checking: parameterized classes, per-specialization static members, typedef of a specialization.
// Expected values: IEEE 1800-2017 8.25 (each distinct set of parameter values is its own specialization with its
// own static members; Stack == Stack#(int,4)), 8.10, 20.6.1 ($typename: only content is checked, as 20.6.1 does not fix the layout:
// the class name and its parameter values appear), 20.7 ($size)
`define CHK(g, e) if ((g) !== (e)) begin errs++; $error("check failed at line %0d", `__LINE__); end
module svlrm_c8e;
  int errs; int fulls;
  class Stack #(type T = int, int DEPTH = 4);
    T data[$];
    function bit push(T v); if (data.size() < DEPTH) begin data.push_back(v); return 1; end else return 0; endfunction
    function T pop(); T t = data[$]; void'(data.pop_back()); return t; endfunction
    function int size(); return data.size(); endfunction
    static int instances;
    function new(); instances++; endfunction
  endclass
  typedef Stack#(string, 2) sstack_t;
  Stack s1; Stack#(int, 2) s2; sstack_t s3; Stack#(real) s4; Stack#(int, 4) s5;
  class Wrapper #(parameter int N = 3);
    int arr[N];
    function int total(); int t = 0; foreach (arr[i]) t += arr[i]; return t; endfunction
  endclass
  Wrapper#() w1; Wrapper#(5) w2;
  function automatic bit has(string s, string sub);
    for (int i = 0; i + sub.len() <= s.len(); i++)
      if (s.substr(i, i + sub.len() - 1) == sub) return 1;
    return 0;
  endfunction
  initial begin
    string tn; int n; string str; real rv;
    s1 = new; s2 = new; s3 = new; s4 = new; s5 = new;
    fulls = 0;
    for (int i = 1; i <= 5; i++) if (!s1.push(i)) fulls++;
    `CHK(fulls, 1)
    `CHK(s1.size(), 4) n = s1.pop(); `CHK(n, 4)
    fulls = 0;
    if (!s2.push(10)) fulls++; if (!s2.push(20)) fulls++; if (!s2.push(30)) fulls++;
    `CHK(fulls, 1) `CHK(s2.size(), 2)
    void'(s3.push("a")); void'(s3.push("b")); void'(s3.push("c"));
    str = s3.pop(); `CHK(str, "b") `CHK(s3.size(), 1)
    void'(s4.push(1.5)); rv = s4.pop(); if (rv != 1.5) begin errs++; $error("real pop"); end
    `CHK(Stack#(int,2)::instances, 1) `CHK(Stack#(int)::instances, 2) `CHK(sstack_t::instances, 1)
    `CHK(Stack#(real)::instances, 1)
    tn = $typename(s2);
    `CHK(has(tn, "Stack"), 1) `CHK(has(tn, "int"), 1) `CHK(has(tn, "2"), 1)
    w1 = new; w2 = new;
    foreach (w1.arr[i]) w1.arr[i] = i + 1;
    foreach (w2.arr[i]) w2.arr[i] = i + 1;
    `CHK(w1.total(), 6) `CHK(w2.total(), 15) `CHK($size(w1.arr), 3) `CHK($size(w2.arr), 5)
    if (errs == 0) $display("PASSED");
  end
endmodule
