// An element of a dynamic array of values wider than 64 bits in a constraint is reported (it used to
// crash the code generator)
class Foo;
  rand bit [65:0] q[$];
  function new;
    q = '{3{0}};
  endfunction
  constraint c { q[0] == 0; q[1] == 1; }
endclass
module svcrvwidearr1;
  Foo foo;
  initial begin
    foo = new;
    void'(foo.randomize());
    $display("not reached");
  end
endmodule
