module ifc2;
  interface class IA;
    pure virtual function int f();
  endclass

  class C1 implements IA;
  endclass

  class C4 implements IA;
    virtual function int f(int a);
      return a;
    endfunction
  endclass

  virtual class Abstract implements IA;
  endclass

  interface class P1#(type T = logic);
  endclass

  interface class P2#(type T = logic);
  endclass

  interface class P3 extends P1#(int), P2#(int);
  endclass

  interface class P4 extends P1#(int), P2#(int);
    typedef int T;
  endclass

  interface class R1;
    pure virtual function void m();
  endclass

  interface class R2;
    pure virtual function int m();
  endclass

  class RC implements R1, R2;
    virtual function void m();
    endfunction
  endclass

  IA h;
  initial h = new;
endmodule
