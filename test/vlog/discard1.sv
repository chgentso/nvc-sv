// IEEE 1800-2017 13.4.1: calling a function with a return value as a
// statement is legal but shall give a warning; casting the call to void
// discards the value without one.
class C;
  function int m();
    return 1;
  endfunction
  function void v();
  endfunction
endclass

module discard1;
  function int f();
    return 1;
  endfunction
  function void g();
  endfunction
  initial begin
    automatic C c = new;
    f();                                // Warning
    c.m();                              // Warning
    void'(f());                         // OK
    void'(c.m());                       // OK
    g();                                // OK
    c.v();                              // OK
  end
endmodule
