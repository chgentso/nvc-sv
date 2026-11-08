module cons2;
  class A;
    rand int x;
    extern constraint c1;
    constraint c2;
    extern function int f();
    pure constraint p1;
  endclass

  virtual class B;
    rand int x;
    pure constraint p1;
    pure constraint p2;
  endclass

  class C extends B;
    constraint p1 { x > 1; }
  endclass

  class D extends B;
    constraint p1 { x > 1; }
    constraint p2 { x > 1; }
  endclass
endmodule
