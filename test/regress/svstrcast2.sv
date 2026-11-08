// A cast to string gives a string, so it compares as a string
// IEEE 1800-2017 6.24.1 (cast operator: casting an integral value to
// string converts it as an assignment to a string variable would), 6.16
// (string operators: ==, < compare strings when either operand is a string)
module svstrcast2;
  string s = "AB";
  int errs;
  initial begin
    if (!(string'(65) == "A")) begin errs++; $display("FAILED =="); end
    if (!("A" == string'(65))) begin errs++; $display("FAILED == swapped"); end
    if (!(string'(16'h4142) == s)) begin errs++; $display("FAILED == variable"); end
    if (!(string'(65) < "B")) begin errs++; $display("FAILED <"); end
    if (string'(65) != "A") begin errs++; $display("FAILED !="); end
    if (({string'(65), "B"}) != "AB") begin errs++; $display("FAILED concat"); end
    if (errs == 0) $display("PASSED");
  end
endmodule
