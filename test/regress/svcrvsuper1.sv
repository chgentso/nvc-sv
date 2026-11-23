// super can be used in a constraint of a derived class (IEEE 1800-2017 8.15)
class Base;
  rand int x;
  function int limit; return 10; endfunction
endclass
class Derived extends Base;
  rand int y;
  constraint c { y < super.limit(); y > 0; x == y; }
endclass
module svcrvsuper1;
  Derived d;
  int fails = 0;
  initial begin
    d = new;
    repeat (20) begin
      if (!d.randomize() || d.y >= 10 || d.y <= 0 || d.x != d.y) begin fails++; $error("x=%0d y=%0d", d.x, d.y); end
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
