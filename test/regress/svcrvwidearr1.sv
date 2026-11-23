// An element of a fixed size array of values wider than 64 bits with a random index in a constraint is reported (it used to
// crash the code generator)
class Foo;
  rand bit [65:0] q[4];
  rand int n;
  constraint c { n inside {[0:3]}; q[n] == 1; }
endclass
module svcrvwidearr1;
  Foo foo;
  initial begin
    foo = new;
    void'(foo.randomize());
    $display("not reached");
  end
endmodule
