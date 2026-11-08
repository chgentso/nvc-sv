module ifc1;
  interface class IA;
    pure virtual function int f();
    int x;
    function int g();
      return 1;
    endfunction
  endclass

  class NotIface;
  endclass

  class C2 implements NotIface;
  endclass

  class C3 extends IA;
  endclass

  interface class IB implements IA;
  endclass
endmodule
