// randomize() called through a null handle is not a crash: it returns 0 (with a warning), and the same
// handle works once it holds an object (IEEE 1800-2017 18.6.1: 0 when the object cannot be randomized).
class A;
  rand bit [7:0] x;
  constraint c { x > 5; x < 9; }
endclass

module svcrvnullobj1;
  A a;
  int r;
  initial begin
    a = null;
    r = a.randomize();
    if (r != 0) $error("randomize on null returned %0d", r);
    a = new;
    r = a.randomize();
    if (r != 1 || a.x < 6 || a.x > 8) $error("randomize on object: %0d x=%0d", r, a.x);
    else $display("PASSED");
  end
endmodule
