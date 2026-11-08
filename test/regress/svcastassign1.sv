// Expected values: IEEE 1800-2017 6.24.1 (cast), 6.12.2 / 11.8.2 (real to integer conversion
// rounds to nearest, ties away from zero), 10.3 (continuous assignment re-evaluates when
// an operand changes). Continuous assignment of a cast expression.
`timescale 1ns/1ns
module svcastassign1;
  real x = 1.5;
  int d, e;
  int errors = 0;
  assign d = int'(x * 2.0);
  assign e = int'(x);
  initial begin
    #1;
    // x = 1.5: int'(3.0) = 3, int'(1.5) = 2 (tie away from zero)
    if (d !== 3) begin $error("d = %0d, expected 3", d); errors++; end
    if (e !== 2) begin $error("e = %0d, expected 2", e); errors++; end
    x = 3.25;
    #1;
    // x = 3.25: int'(6.5) = 7 (tie away from zero), int'(3.25) = 3
    if (d !== 7) begin $error("d = %0d, expected 7", d); errors++; end
    if (e !== 3) begin $error("e = %0d, expected 3", e); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
