// Self-checking. IEEE 1800-2017 11.8.2: a negated or inverted integral
// expression assigned to a real is converted by its value.
module svrealneg1;
  real r;
  integer i = 5;
  function real f(real x); f = x; endfunction
  task t(real x, output real y); y = x; endtask
  initial begin
    r = -1;
    if (r != -1.0) $error("FAIL a %f", r);
    r = f(-i);
    if (r != -5.0) $error("FAIL b %f", r);
    t(-1, r);
    if (r != -1.0) $error("FAIL c %f", r);
    t(~3'd1, r);
    if (r != 6.0) $error("FAIL d %f", r);
    $display("PASSED");
  end
endmodule
