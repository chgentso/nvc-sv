module cons1;
  class A;
    rand int x;
    extern constraint c1;
    constraint c2;
    constraint c3 { x > 0; }
    extern constraint c4 { x < 5; }
  endclass

  constraint A::c1 { x > 1; }

  constraint A::c3 { x > 2; }

  constraint A::cz { x > 2; }

  constraint B::c1 { x > 2; }

  static constraint A::c2 { x > 1; }
endmodule
