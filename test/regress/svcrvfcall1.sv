// A function whose argument is a random variable is evaluated after that variable is solved
// (IEEE 1800-2017 18.5.12).
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
    repeat (10) begin
      if (!a.randomize()) $error("randomize failed");
      else if (a.x >= 10 || a.y != a.x + 8'd1) $error("x=%0d y=%0d", a.x, a.y);
    end
    $display("PASSED");
  end
endmodule
