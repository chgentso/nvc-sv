// %c of a string variable prints its last character, and %c honours the
// field width
// IEEE 1800-2017 21.2.1.2 (Table 21-1: %c displays the character whose
// code is the low 8 bits of the value; a string's last character is its
// low byte, 6.16), 21.2.1.3 (field width, - for left justification)
module svpctc2;
  string s = "xyz", e = "";
  int errs;
  task automatic eq(string got, string exp);
    if (got != exp) begin $display("FAILED got '%s' expected '%s'", got, exp); errs++; end
  endtask
  initial begin
    eq($sformatf("[%c]", s), "[z]");
    eq($sformatf("[%3c]", s), "[  z]");
    eq($sformatf("[%-3c]", s), "[z  ]");
    eq($sformatf("[%c]", e), "[]");
    eq($sformatf("[%3c]", 65), "[  A]");
    eq($sformatf("[%-3c|%0c]", 65, 66), "[A  |B]");
    if (errs == 0) $display("PASSED");
  end
endmodule
