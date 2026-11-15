// IEEE 1800-2017 6.24.1: the width of a size cast must be positive.
module castsize1;
  int a;
  reg [3:0] b;
  initial begin
    b = (-1)'(a);                               // Error
    b = 0'(a);                                  // Error
    b = 4'(a);                                  // OK
  end
endmodule
