// IEEE 1800-2017 8.15, 8.20, 26.3: super.new is the first statement of a
// constructor, only a method is virtual and a class cannot import a
// package.
package pkg;
  int x;
endpackage
module classitem1;
  class Base;
    int b;
    function new(int a);
      b = a;
    endfunction
  endclass
  class Cls extends Base;
    int c;
    virtual int member;                         // Error
    import pkg::*;                              // Error
    function new();
      c = 10;
      super.new(c);                             // Error
    endfunction
    virtual function void ok();                 // OK
    endfunction
  endclass
  class Good extends Base;
    function new();
      super.new(1);                             // OK
    endfunction
  endclass
endmodule
