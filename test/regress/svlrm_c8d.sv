// Self-checking: data hiding (local/protected), const and static const properties.
// Expected values: IEEE 1800-2017 8.18 (local/protected access from inside the class and from subclasses),
// 8.9 (static members, accessible through instance or class scope), 8.20 (const properties)
`define CHK(g, e) if ((g) !== (e)) begin errs++; $error("check failed at line %0d", `__LINE__); end
module svlrm_c8d;
  int errs;
  class A;
    local int secret = 1;
    protected int prot = 2;
    int pub = 3;
    const int K = 11;
    static const int SK = 22;
    function int get_secret(); return secret; endfunction
    function int get_prot(); return prot; endfunction
    local function int hidden(); return 99; endfunction
    function int call_hidden(); return hidden(); endfunction
  endclass
  class B extends A;
    function int peek(); return prot + pub; endfunction
    function void set_prot(int v); prot = v; endfunction
  endclass
  A a; B b;
  initial begin
    a = new; b = new;
    `CHK(a.get_secret(), 1) `CHK(a.get_prot(), 2) `CHK(a.pub, 3)
    `CHK(b.peek(), 5) `CHK(b.call_hidden(), 99)
    b.set_prot(40); `CHK(b.get_prot(), 40) `CHK(a.get_prot(), 2)
    `CHK(a.K, 11) `CHK(A::SK, 22) `CHK(b.SK, 22)
    a.pub = 4; `CHK(a.pub, 4) `CHK(b.pub, 3)
    if (errs == 0) $display("PASSED");
  end
endmodule
