// Self-checking: class basics, constructors, this, static members, handle semantics.
// Expected values: IEEE 1800-2017 8.2-8.7 (constructors, super.new, default arguments), 8.9 (static properties
// and methods), 8.11 (this), 8.4 (handles; null), 11.4.5 (handle equality)
`define CHK(g, e) if ((g) !== (e)) begin errs++; $error("check failed at line %0d", `__LINE__); end
module svlrm_c8a;
  int errs;
  class Base;
    int a = 1;
    static int count;
    int id;
    function new(int x = 5);
      a = x; count++; id = count;
    endfunction
    virtual function string show(); return $sformatf("Base a=%0d id=%0d", a, id); endfunction
    static function int get_count(); return count; endfunction
    function Base self(); return this; endfunction
    task t(int n); a += n; endtask
  endclass
  class Derived extends Base;
    int b = 2;
    function new(int x = 7, int y = 9);
      super.new(x);
      b = y;
    endfunction
    virtual function string show(); return {super.show(), $sformatf(" Derived b=%0d", b)}; endfunction
  endclass
  Base p; Derived d; Base q;
  initial begin
    p = new; `CHK(p.show(), "Base a=5 id=1")
    p = new(10); `CHK(p.show(), "Base a=10 id=2")
    d = new; `CHK(d.show(), "Base a=7 id=3 Derived b=9")
    d = new(1, 2); `CHK(d.show(), "Base a=1 id=4 Derived b=2")
    `CHK(Base::count, 4) `CHK(Base::get_count(), 4)
    `CHK(p.get_count(), 4)
    p = d; `CHK(p.show(), "Base a=1 id=4 Derived b=2")
    `CHK(p == d, 1)
    q = p.self(); `CHK(q == d, 1)
    p.t(5); `CHK(d.a, 6)
    q = new; `CHK(q != p, 1)
    q = null; `CHK(q == null, 1)
    p = null;
    begin Base x; Base y; x = new(3); y = x; y.a = 99; `CHK(x.a, 99) end
    begin Derived dd; Base bb; dd = new(4, 5); bb = dd; `CHK(bb.a, 4) end
    `CHK(Base::count, 7)
    d = new(); `CHK(d.a, 7) `CHK(d.b, 9)
    if (errs == 0) $display("PASSED");
  end
endmodule
