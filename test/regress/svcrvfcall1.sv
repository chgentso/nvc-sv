// A function whose argument is a random variable should be evaluated after that variable is solved
// (IEEE 1800-2017 18.5.12).  That is not supported yet: the call is evaluated before solving, with the
// value the variable has, and a warning says so (it used to be silent).
class A;
  rand bit [7:0] x, y;
  function bit [7:0] f(bit [7:0] v); return v + 1; endfunction
  constraint c { y == f(x); x < 10; }
endclass

module svcrvfcall1;
  A a;
  initial begin
    a = new;
    a.x = 4;
    if (!a.randomize()) $error("randomize failed");
    else if (a.x >= 10) $error("x=%0d", a.x);
    else $display("PASSED");
  end
endmodule
