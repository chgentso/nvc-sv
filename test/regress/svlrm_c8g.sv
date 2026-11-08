// Self-checking: virtual dispatch from constructors, default arguments of virtual methods, super chains,
// static methods.
// Expected values: IEEE 1800-2017 8.7 (super.new() is called first; the derived class's property initializers
// run after it returns, so inside a virtual call made by the base constructor n is still 0),
// 8.20 (virtual dispatch uses the object's actual class, also inside constructors;
// default argument values come from the static type of the handle used in the call), 8.15 (super), 8.10 (static)
`define CHK(g, e) if ((g) !== (e)) begin errs++; $error("check failed at line %0d: got '%s'", `__LINE__, g); end
module svlrm_c8g;
  int errs;
  string tr;
  class A;
    function new(); init(); endfunction
    virtual function void init(); tr = {tr, "A::init;"}; endfunction
    virtual function void f(int x = 1, int y = 2); tr = {tr, $sformatf("A::f %0d %0d;", x, y)}; endfunction
    function void g(); f(); endfunction
    virtual function string who(); return "A"; endfunction
    function string call_who(); return who(); endfunction
    static function void sf(); tr = {tr, "A::sf;"}; endfunction
  endclass
  class B extends A;
    int n = 5;
    function new(); super.new(); tr = {tr, $sformatf("B ctor n=%0d;", n)}; endfunction
    virtual function void init(); tr = {tr, $sformatf("B::init n=%0d;", n)}; endfunction
    virtual function void f(int x = 10, int y = 20); tr = {tr, $sformatf("B::f %0d %0d;", x, y)}; super.f(x, y); endfunction
    virtual function string who(); return {"B>", super.who()}; endfunction
  endclass
  class C extends B;
    virtual function string who(); return {"C>", super.who()}; endfunction
    static function void sf(); tr = {tr, "C::sf;"}; A::sf(); endfunction
  endclass
  A a; B b; C c;
  initial begin
    tr = ""; b = new;
    `CHK(tr, "B::init n=0;B ctor n=5;")
    a = b;
    tr = ""; a.f(); `CHK(tr, "B::f 1 2;A::f 1 2;")
    tr = ""; a.f(3); `CHK(tr, "B::f 3 2;A::f 3 2;")
    tr = ""; a.f(3, 4); `CHK(tr, "B::f 3 4;A::f 3 4;")
    tr = ""; b.f(); `CHK(tr, "B::f 10 20;A::f 10 20;")
    tr = ""; a.g(); `CHK(tr, "B::f 1 2;A::f 1 2;")
    tr = ""; c = new; `CHK(tr, "B::init n=0;B ctor n=5;")
    a = c;
    `CHK(a.who(), "C>B>A") `CHK(c.call_who(), "C>B>A") `CHK(b.who(), "B>A")
    tr = ""; C::sf(); `CHK(tr, "C::sf;A::sf;")
    tr = ""; A::sf(); `CHK(tr, "A::sf;")
    tr = ""; a = new; `CHK(tr, "A::init;")
    tr = ""; a.f(); `CHK(tr, "A::f 1 2;")
    `CHK(a.who(), "A")
    if (errs == 0) $display("PASSED");
  end
endmodule
