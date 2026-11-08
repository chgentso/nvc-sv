// Property initialisers run after the base class constructor returns, and a
// virtual call from a constructor dispatches on the class of the object
// IEEE 1800-2017 8.7 (constructors: super.new first, then the properties
// of the class are initialised, then the rest of the constructor), 8.20
// (virtual methods), 8.15 (super)
module svctorinit1;
  int cnt = 0;
  int errs = 0;
  class B;
    int x = 5;
    int id = cnt++;
    function new(); x = x + 10; endfunction
  endclass
  class D extends B;            // no constructor
    int y = x + 1;
    string s = "hi";
    int q[$] = '{1, 2};
    function int getx(); return x; endfunction
    int z = getx() * 2;
    function D clone();
      D c = new;                // new inside a method of the same class
      return c;
    endfunction
  endclass
  class E extends D;
    int w = y + z;
    function new(); super.new(); w++; endfunction
  endclass
  class P;
    int a = 3;
    int b = a * 2;
  endclass
  string tr;
  class VB;
    function new(); tr = {tr, $sformatf("init %0d;", get())}; endfunction
    virtual function int get(); return -1; endfunction
  endclass
  class VD extends VB;
    int n = 7;
    function new(); super.new(); tr = {tr, $sformatf("ctor %0d;", get())}; endfunction
    virtual function int get(); return n; endfunction
  endclass
  class VE extends VD;          // no constructor
    int m = n + 1;
    virtual function int get(); return m; endfunction
  endclass
  initial begin
    automatic D d = new;
    D d2;
    automatic E e = new;
    automatic P p = new;
    if (d.x != 15 || d.y != 16 || d.z != 30 || d.s != "hi" || d.q.size() != 2) begin errs++; $display("d %0d %0d %0d", d.x, d.y, d.z); end
    d.y = 99; d.x = 1;
    d2 = d.clone();
    if (d2.x != 15 || d2.y != 16 || d2.z != 30 || d.y != 99 || d.x != 1) begin errs++; $display("d2 %0d %0d %0d", d2.x, d2.y, d2.z); end
    if (e.w != 47) begin errs++; $display("e %0d", e.w); end
    if (p.b != 6) errs++;
    if (cnt != 3 || e.id != 1) begin errs++; $display("cnt %0d", cnt); end
    begin
      VD vd; VE ve;
      tr = ""; vd = new;
      if (tr != "init 0;ctor 7;") begin errs++; $display("vd %s", tr); end
      tr = ""; ve = new;
      if (tr != "init 0;ctor 0;" || ve.m != 8) begin errs++; $display("ve %s", tr); end
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
