// A randc variable in a class whose constraints call a function on a random
// argument (solved in two stages, IEEE 1800-2017 18.5.12) still goes through
// every value of its cycle once (18.4.2)
class A;
  randc bit [3:0] r;
  rand bit [7:0] x, y;
  function bit [7:0] f(bit [7:0] v); return v + 1; endfunction
  constraint c { y == f(x); x < 10; }
endclass

module svcrvrandcstaged1;
  A a;
  bit [15:0] seen;
  initial begin
    a = new;
    for (int cyc = 0; cyc < 3; cyc++) begin
      seen = 0;
      repeat (16) begin
        if (!a.randomize()) $error("randomize failed");
        else if (a.x >= 10 || a.y != a.x + 8'd1) $error("x=%0d y=%0d", a.x, a.y);
        if (seen[a.r]) $error("cycle %0d: r=%0d drawn twice", cyc, a.r);
        seen[a.r] = 1;
      end
      if (seen != 16'hffff) $error("cycle %0d: seen=%h", cyc, seen);
    end
    $display("PASSED");
  end
endmodule
