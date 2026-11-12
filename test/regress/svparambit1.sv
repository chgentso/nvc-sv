// A bit select of a parameter with an implicit scalar type
module svparambit1;
  parameter A = 1'b0;
  parameter B = 1'b1;
  wire a = A[0];
  wire b = B[0];

  initial begin
    #1;
    if (a !== 1'b0 || b !== 1'b1) $fatal(1, "a=%b b=%b", a, b);
    $display("PASSED");
  end
endmodule
