// IEEE 1800-2017 8.4: a class handle is not an integral value, so the
// integral conversions of 21.2.1 are not defined for it.  nvc prints its
// number with a warning, prints a handle given as the first argument
// of $display as if by %p (as it already prints a null handle) and pads
// a null handle with spaces for every conversion.
module svhandlefmt1;
  class C; int x = 5; endclass
  C c, cn;
  initial begin
    c = new;
    $display(c);
    $display(cn);
    $display("[%8x] [%5d]", cn, cn);
    $display("%d", c);
    $display("PASSED");
  end
endmodule
