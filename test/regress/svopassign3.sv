// Self-checking. IEEE 1800-2017 11.4.1: a compound assignment is signed only
// if both its target and its operand are.
module svopassign3;
  reg signed [3:0] sv[0:1];
  integer index;
  initial begin
    index = 1;
    sv[1] = -6;
    sv[index] /= 4'sd3;
    if (sv[1] !== -4'sd2) $error("FAIL signed %h", sv[1]);
    sv[1] = -6;
    sv[index] /= 4'd3;
    if (sv[1] !== 4'd3) $error("FAIL mixed %h", sv[1]);
    sv[1] = -6;
    sv[index] %= 4'd4;
    if (sv[1] !== 4'd2) $error("FAIL mod %h", sv[1]);
    $display("PASSED");
  end
endmodule
