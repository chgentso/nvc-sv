// Self-checking: shallow copy (new <handle>), user clone, extern (out-of-block) methods.
// Expected values: IEEE 1800-2017 8.12 (shallow copy: nested handles are shared, not duplicated),
// 8.24 (out-of-block declarations), 8.9 (static method), 8.11 (this)
`define CHK(g, e) if ((g) !== (e)) begin errs++; $error("check failed at line %0d", `__LINE__); end
module svlrm_c8c;
  int errs;
  class Inner; int v = 1; endclass
  class Outer;
    int x = 10;
    Inner in;
    function new(); in = new; endfunction
    function Outer clone();
      Outer c = new; c.x = x; c.in.v = in.v; return c;
    endfunction
    extern function void bump(int d);
    extern function int get();
    extern static function Outer make(int x);
  endclass
  function void Outer::bump(int d); x += d; endfunction
  function int Outer::get(); return x; endfunction
  function Outer Outer::make(int x); Outer o = new; o.x = x; return o; endfunction
  Outer a, b, c, d;
  initial begin
    a = new; a.x = 5; a.in.v = 50;
    b = new a;
    `CHK(b.x, 5) `CHK(b.in == a.in, 1)
    b.x = 6; b.in.v = 60;                 // the Inner object is shared by a and b
    `CHK(a.x, 5) `CHK(a.in.v, 60) `CHK(b.x, 6) `CHK(b.in.v, 60)
    c = a.clone(); `CHK(c.in != a.in, 1) `CHK(c.in.v, 60)
    c.in.v = 70; c.x = 7;                 // the clone is deep for Inner
    `CHK(a.x, 5) `CHK(a.in.v, 60) `CHK(c.x, 7) `CHK(c.in.v, 70)
    a.bump(3); `CHK(a.get(), 8)
    d = Outer::make(42); `CHK(d.get(), 42)
    d.bump(-2); `CHK(d.x, 40)
    begin
      Outer e;
      e = new b; `CHK(e.x, 6) `CHK(e.in.v, 60)
      e.in.v = 1; `CHK(b.in.v, 1)
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
