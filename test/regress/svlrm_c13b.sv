`timescale 1ns/1ns
module svlrm_c13b;
  // Self-checking test. Expected values: IEEE 1800-2017 13.3-13.5 (subroutines, const ref, ref,
  // 13.5.2 pass by reference; 13.5.3 default arguments evaluated at each call), 13.4 (recursion),
  // 13.4.1 (return statement), 6.21 (static vs automatic variables), 9.3.2 (fork/join, join_none),
  // 9.6.2 (disable of a named block / fork), 9.4.1 (delay control inside tasks), 7.5 (new[n](src)),
  // 11.4.3 (a + 1 in a 4-bit return: result truncated, x propagates).
  // Default argument `b = a + 1` refers to the earlier formal `a`: 13.5.3 evaluates a default in the
  // scope of the declaration, where `a` is not visible, so nvc accepts it as an extension with a warning.
  int g = 5;
  function automatic int f_ref(const ref int a[3]); return a[0] + a[1] + a[2]; endfunction
  function automatic int f_expr(int a, int b = a + 1); return a * b; endfunction
  function automatic int f_glob(int a = g); return a * 2; endfunction
  task automatic wait_and_set(ref int v, input int t, input int val); #t v = val; endtask
  time loops[$];
  int loopi[$];
  time ticks[$];
  task automatic loop_task(int n); for (int i = 0; i < n; i++) begin #1; loops.push_back($time); loopi.push_back(i); end endtask
  task automatic stop_task(); forever begin #2; ticks.push_back($time); end endtask
  function automatic int fib(int n); if (n < 2) return n; return fib(n - 1) + fib(n - 2); endfunction
  int st_log[$], au_log[$];
  task static t_static(); static int cnt = 0; cnt++; st_log.push_back(cnt); endtask
  task automatic t_auto(); automatic int cnt = 0; cnt++; au_log.push_back(cnt); endtask
  function automatic string join_s(string a[]); string r = ""; foreach (a[i]) r = {r, a[i], (i < a.size() - 1) ? "," : ""}; return r; endfunction
  function automatic int min_of(int a, int b); return a < b ? a : b; endfunction
  function automatic void swap(ref int a, ref int b); int t = a; a = b; b = t; endfunction
  function automatic logic [3:0] lf(logic [3:0] a); return a + 1; endfunction
  int arr3[3] = '{1, 2, 3};
  int p, q, p_at5;
  int errs;
  function automatic void ck(bit ok, string m);
    if (!ok) begin errs++; $error("%s", m); end
  endfunction
  initial begin
    ck(f_ref(arr3) == 6, "const ref array");
    ck(f_expr(3) == 12 && f_expr(3, 5) == 15, "default argument from earlier formal");
    g = 7; ck(f_glob() == 14, "default argument evaluated at call time");
    p = 0; fork wait_and_set(p, 3, 42); #5 p_at5 = p; join
    ck(p_at5 == 42 && $time == 5, "fork/join with delayed ref write");
    loop_task(3);
    ck(loops.size() == 3 && loops[0] == 6 && loops[1] == 7 && loops[2] == 8, "loop task times");
    ck(loopi.size() == 3 && loopi[0] == 0 && loopi[1] == 1 && loopi[2] == 2, "loop task indices");
    fork : stopper stop_task(); join_none
    #7 disable stopper;
    ck(ticks.size() == 3 && ticks[0] == 10 && ticks[1] == 12 && ticks[2] == 14, "ticks before disable at 15");
    #5;
    ck(ticks.size() == 3, "no tick after disable");
    ck(fib(10) == 55, "fib");
    t_static(); t_static(); t_auto(); t_auto();
    ck(st_log.size() == 2 && st_log[0] == 1 && st_log[1] == 2, "static counter persists");
    ck(au_log.size() == 2 && au_log[0] == 1 && au_log[1] == 1, "automatic counter restarts");
    begin automatic string s[3] = '{"a", "b", "c"}; string d[]; d = new[3](s); ck(join_s(d) == "a,b,c", "string array join"); end
    ck(min_of(3, 4) == 3, "min_of");
    p = 1; q = 2; swap(p, q); ck(p == 2 && q == 1, "swap by ref");
    ck(lf(4'hf) === 4'h0 && lf(4'bx) === 4'bxxxx, "4-bit function result");
    if (errs == 0) $display("PASSED");
  end
endmodule
