// IEEE 1800-2017 6.21: a variable declared in a module is static, only the
// variables in a subroutine or block can be automatic.
module lifetime2;
  automatic int i;                              // Error
  static int s;                                 // OK
  function automatic int f(int a);
    automatic int x = a;                        // OK
    return x;
  endfunction
  initial begin
    automatic int z = 1;                        // OK
    $display("%0d", z);
  end
endmodule
