// IEEE 1800-2017 13.5.3: the default value of an argument is evaluated
// in the scope containing the subroutine declaration, where the other
// arguments are not visible, so a default that refers to an earlier
// argument is an extension, which nvc accepts with a warning rather
// than silently.
module svdefarg1;
  int g = 4;
  function automatic int f(int a, int b = a + 1, int c = g);
    return a + b + c;
  endfunction
  initial begin
    if (f(1) == 7) $display("PASSED");
    else $display("FAILED %0d", f(1));
  end
endmodule
