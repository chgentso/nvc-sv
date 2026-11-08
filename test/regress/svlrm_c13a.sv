module svlrm_c13a;
  // Self-checking test. Expected values: IEEE 1800-2017 13.3 / 13.4 (tasks and functions, recursion,
  // function-name return variable, void functions, calling a function as a statement with void'()),
  // 13.5 (argument passing by value, input/output/inout copy-out, ref aliasing, 13.5.2),
  // 13.5.3 (default argument values), 13.5.4 (named binding in any order), 6.21 (static variables
  // of a static function persist), 7.5/7.6 (dynamic and fixed arrays as arguments), 7.10 (queue by ref).
  function automatic int add(int a, int b = 10); return a + b; endfunction
  function automatic int fact(int n); return n <= 1 ? 1 : n * fact(n - 1); endfunction
  function void vf(input int x, output int y, inout int z); y = x * 2; z += x; endfunction
  task automatic tsk(input int a, output int b, ref int c); b = a + 1; c = c + a; #1; endtask
  function automatic int sum_arr(int a[]); int s = 0; foreach (a[i]) s += a[i]; return s; endfunction
  function automatic int sum_fixed(int a[4]); int s = 0; foreach (a[i]) s += a[i]; return s; endfunction
  function automatic void fill(ref int a[4]); foreach (a[i]) a[i] = i * i; endfunction
  function automatic void qpush(ref int q[$], input int v); q.push_back(v); endfunction
  function automatic string greet(string n = "world"); return {"hello ", n}; endfunction
  function automatic real half(real r); return r / 2.0; endfunction
  function automatic bit [7:0] rev(bit [7:0] v); for (int i = 0; i < 8; i++) rev[i] = v[7 - i]; endfunction
  function automatic int named(int a, int b, int c = 3); return a * 100 + b * 10 + c; endfunction
  function static int counter(); static int c = 0; c++; return c; endfunction
  bit noret_called;
  function automatic void noret(); noret_called = 1; endfunction
  function int implicit_ret(); implicit_ret = 5; implicit_ret++; endfunction
  int x, y, z, c;
  int arr4[4]; int dyn[]; int q[$];
  int errs, c1, c2, c3;
  function automatic void ck(bit ok, string m);
    if (!ok) begin errs++; $error("%s", m); end
  endfunction
  initial begin
    ck(add(1, 2) == 3 && add(5) == 15 && fact(6) == 720, "add/default/fact");
    x = 3; z = 10; vf(x, y, z); ck(x == 3 && y == 6 && z == 13, "output and inout");
    c = 100; tsk(5, y, c); ck(y == 6 && c == 105, "task with output and ref");
    arr4 = '{1, 2, 3, 4}; dyn = '{5, 6};
    ck(sum_arr(dyn) == 11 && sum_arr(arr4) == 10 && sum_fixed(arr4) == 10, "array arguments");
    fill(arr4); ck(arr4[0] == 0 && arr4[1] == 1 && arr4[2] == 4 && arr4[3] == 9, "ref array");
    qpush(q, 4); qpush(q, 5); ck(q.size() == 2 && q[0] == 4 && q[1] == 5, "ref queue");
    ck(greet() == "hello world" && greet("sv") == "hello sv", "string default");
    ck(half(5.0) == 2.5, "real function");
    ck(rev(8'b00010111) === 8'b11101000, "bit reversal");
    ck(named(1, 2) == 123 && named(.b(5), .a(4)) == 453, "named args");
    ck(named(.c(9), .a(1), .b(2)) == 129, "named args any order");
    c1 = counter(); c2 = counter(); c3 = counter(); ck(c1 == 1 && c2 == 2 && c3 == 3, "static variable persists");
    noret(); void'(add(1, 1)); ck(noret_called, "void function called");
    ck(implicit_ret() == 6, "function name variable");
    ck(add(add(1, 2), add(3)) == 16, "nested calls");
    ck(fact(fact(3)) == 720, "nested recursion");
    if (errs == 0) $display("PASSED");
  end
endmodule
