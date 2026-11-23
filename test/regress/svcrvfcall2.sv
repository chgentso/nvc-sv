// A call whose random argument is an element of an array is not staged yet: it is evaluated before
// solving with the current value, and a warning says so.
class A;
  rand bit [7:0] arr[2];
  rand bit [7:0] y;
  function bit [7:0] f(bit [7:0] v); return v + 1; endfunction
  constraint c { y == f(arr[0]); }
endclass

module svcrvfcall2;
  A a;
  initial begin
    a = new;
    a.arr[0] = 4;
    if (!a.randomize()) $error("randomize failed");
    else $display("PASSED");
  end
endmodule
