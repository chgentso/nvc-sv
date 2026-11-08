// Virtual method called from a constructor.
// Expected values: IEEE 1800-2017 8.20 (a virtual method call dispatches on the type of the object, which is
// the derived class from allocation on, so base::new calls derived::clear), 8.7 (constructors, super.new
// runs first), 8.4 (src is still null while base::new runs, so the call is guarded), 6.16 (strings).
module svctorvirt1;
  string log = "";
  class idc;
    int v = 5;
    function void clear(); v = 0; log = {log, "idc clear;"}; endfunction
  endclass
  class base;
    function new(); log = {log, "base new;"}; clear(); endfunction
    virtual function void clear(); log = {log, "base clear;"}; endfunction
  endclass
  class derived extends base;
    idc src;
    function new(); super.new(); src = new; clear(); endfunction
    virtual function void clear();
      super.clear(); log = {log, "derived clear;"};
      if (src != null) src.clear();
    endfunction
  endclass
  initial begin
    string exp;
    automatic derived d = new;
    exp = {"base new;", "base clear;", "derived clear;",
           "base clear;", "derived clear;", "idc clear;"};
    if (log != exp) $error("call sequence \"%s\", expected \"%s\"", log, exp);
    else if (d.src.v != 0) $error("idc.v=%0d, expected 0", d.src.v);
    else $display("PASSED");
  end
endmodule
