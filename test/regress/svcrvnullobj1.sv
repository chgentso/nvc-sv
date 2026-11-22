// randomize() called through a null handle is a null access, not a crash (IEEE 1800-2017 8.4, 18.6)
class A;
  rand bit [7:0] x;
endclass

module svcrvnullobj1;
  A a;
  int r;
  initial begin
    a = null;
    r = a.randomize();
    $display("not reached");
  end
endmodule
