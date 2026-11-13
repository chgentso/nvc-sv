// Self-checking. IEEE 1800-2017 5.9: \xdd in a string literal is a character
// given by one or two hexadecimal digits, including a to f.
module svstrhex1;
  initial begin
    if ("\x9a" !== 8'h9a) $error("FAIL 9a");
    if ("\x7A" !== 8'h7a) $error("FAIL 7A");
    if ("\xf1" !== 8'hf1) $error("FAIL f1");
    if ("\x0f\xFF" !== 16'h0fff) $error("FAIL 0fff");
    $display("PASSED");
  end
endmodule
