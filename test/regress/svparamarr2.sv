// Self-checking. Expected values: IEEE 1800-2017 6.20.2 (a parameter or
// localparam may have an unpacked array type whose value is given by an
// assignment pattern, 10.9.1, with constant expressions as the elements,
// 11.2.1: constant expressions use operators on constants and parameters).
package svparamarr2_pkg;
  parameter int MsgWidth = 64;
  parameter int unsigned Rate [5] = '{
    1344/MsgWidth, 1152/MsgWidth, 1088/MsgWidth, 832/MsgWidth, 576/MsgWidth
  };
  parameter int unsigned Cap [3] = '{ 2 * 128, 2 * 224, 2 * 256 };
endpackage

module svparamarr2 #(parameter int unsigned P [2] = '{ 3 + 4, 5 * 6 });
  import svparamarr2_pkg::*;
  localparam int unsigned L [2] = '{ 1 + 1, 3 * 3 };
  int errs = 0;
  initial begin
    if (Rate[0] != 21 || Rate[1] != 18 || Rate[4] != 9) begin errs++; $error("Rate"); end
    if (Cap[0] != 256 || Cap[2] != 512) begin errs++; $error("Cap"); end
    if (P[0] != 7 || P[1] != 30) begin errs++; $error("P"); end
    if (L[0] != 2 || L[1] != 9) begin errs++; $error("L"); end
    if (errs == 0) $display("PASSED");
  end
endmodule
