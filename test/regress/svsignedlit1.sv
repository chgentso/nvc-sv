// Self-checking. IEEE 1800-2017 11.8.1: an operand is only sign extended if
// every operand of the expression is signed, so a signed literal in an
// expression with an unsigned operand is zero extended.
module svsignedlit1;
  integer r;
  reg [7:0] u = 1;
  initial begin
    r = 4'sb1111 - 1'b1;
    if (r !== 32'he) $error("FAIL minus %h", r);
    r = 4'sb1111 + 4'b0001;
    if (r !== 32'h10) $error("FAIL plus %h", r);
    r = 4'sb1111 * u;
    if (r !== 32'hf) $error("FAIL times %h", r);
    r = 4'sb1111 + 4'sb0001;
    if (r !== 32'h0) $error("FAIL signed %h", r);
    r = 4'sb1111;
    if (r !== 32'hffffffff) $error("FAIL assign %h", r);
    $display("PASSED");
  end
endmodule
