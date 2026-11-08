// Default argument values: expressions may refer to earlier formals, are evaluated at each call.
// Expected values: IEEE 1800-2017 13.5.3 (default argument values, evaluated in the scope of the
// subroutine declaration at call time; named binding with defaults), 8.6 (class methods).
module svdefaultarg1;
  int g = 5;
  int ta, tb;
  int errs = 0;
  function automatic int f_expr(int a, int b = a + 1); return a * b; endfunction
  function automatic int f3(int a, int b = a * 2, int c = a + b); return a * 100 + b * 10 + c; endfunction
  function automatic int f_glob(int a = g); return a * 2; endfunction
  function automatic int f_mix(int a, int b = 3, int c = b + a); return a + b + c; endfunction
  task automatic t(int a, int b = a - 1); ta = a; tb = b; endtask
  class K;
    function int m(int a, int b = a + 100); return a + b; endfunction
  endclass
  K k;
  task automatic chk(string what, int got, int exp);
    if (got !== exp) begin errs++; $error("%s: got %0d, expected %0d", what, got, exp); end
  endtask
  initial begin
    k = new;
    chk("f_expr(3)", f_expr(3), 12);
    chk("f_expr(3,5)", f_expr(3, 5), 15);
    chk("f3(1)", f3(1), 123);
    chk("f3(1,5)", f3(1, 5), 156);
    chk("f3(1,5,7)", f3(1, 5, 7), 157);
    chk("f_glob() g=5", f_glob(), 10);
    g = 7; chk("f_glob() g=7", f_glob(), 14);
    chk("f_mix(1)", f_mix(1), 8);
    chk("f_mix(1,10)", f_mix(1, 10), 22);
    chk("f_mix(.a(2),.c(4))", f_mix(.a(2), .c(4)), 9);
    t(5); chk("t(5).a", ta, 5); chk("t(5).b", tb, 4);
    t(5, 1); chk("t(5,1).a", ta, 5); chk("t(5,1).b", tb, 1);
    chk("k.m(1)", k.m(1), 102);
    chk("k.m(1,2)", k.m(1, 2), 3);
    chk("f_expr(f_expr(2))", f_expr(f_expr(2)), 42);
    if (errs == 0) $display("PASSED");
  end
endmodule
