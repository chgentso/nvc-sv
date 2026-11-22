// A function whose argument is a random variable is evaluated after that variable is solved
// (IEEE 1800-2017 18.5.12).  This is not supported yet: it must be reported, not evaluated on the
// values left by the previous call.
class A;
  rand bit [7:0] x, y;
  function bit [7:0] f(bit [7:0] v); return v + 1; endfunction
  constraint c { y == f(x); x < 10; }
endclass

module svcrvfcall1;
  A a;
  initial begin
    a = new;
    void'(a.randomize());
    $display("not reached");
  end
endmodule
