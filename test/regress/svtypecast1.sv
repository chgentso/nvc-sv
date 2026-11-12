// A cast to the type of an expression
module svtypecast1;
  real x;
  logic [11:0] w;
  var type (x) z;
  logic [7:0] b;

  initial begin
    z = type (z)'(22);
    if (z != 22.0) $fatal(1, "z = %f", z);
    w = type (w)'(8'hff);
    if (w !== 12'h0ff) $fatal(1, "w = %h", w);
    b = type (b)'(9'h1ff);
    if (b !== 8'hff) $fatal(1, "b = %h", b);
    $display("PASSED");
  end
endmodule
