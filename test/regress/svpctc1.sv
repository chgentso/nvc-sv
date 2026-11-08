// %c with strings, vectors and integers; %g / %G / %e / %E real formats
// Expected values: IEEE 1800-2017 21.2.1.2 Table 21-1 (%c: ASCII character of the low 8 bits; a string
// argument is a sequence of bytes, so its last character is the low byte; %e %g are as in C), 21.3.3
// The effect of a field width on %c is not specified and is ignored by the checks (blanks are stripped).
module svpctc1;
  string s = "e";
  string t = "xyz";
  logic [7:0] v = 8'h66;
  logic [15:0] w = 16'h6768;
  int errs = 0;
  function automatic string nb(string x);
    string r = "";
    for (int i = 0; i < x.len(); i++) if (x[i] != " ") r = {r, string'(x[i])};
    return r;
  endfunction
  task automatic chk(string got, string exp);
    if (nb(got) != exp) begin $error("got '%s' expected '%s'", got, exp); errs++; end
  endtask
  initial begin
    chk($sformatf("[%c]", "e"), "[e]");
    chk($sformatf("[%5c]", "e"), "[e]");
    chk($sformatf("[%-5c]", v), "[f]");
    chk($sformatf("[%c|%c]", s, v), "[e|f]");
    chk($sformatf("[%c]", t), "[z]");
    chk($sformatf("[%c]", w), "[h]");
    chk($sformatf("[%c]", 101), "[e]");
    chk($sformatf("[%3c]", 101), "[e]");
    chk($sformatf("[%0c]", 101), "[e]");
    chk($sformatf("[%G] [%G] [%g] [%E] [%e]", 1e-10, 123456789.0, 1e-10, 12.5, 12.5),
        "[1E-10][1.23457E+08][1e-10][1.250000E+01][1.250000e+01]");
    chk($sformatf("[%G]", 0.0001), "[0.0001]");
    chk($sformatf("[%10.3G]", 3.14159), "[3.14]");
    chk($sformatf("%G", 1e20), "1E+20");
    chk($sformatf("%c", "a"), "a");
    if (errs == 0) $display("PASSED");
  end
endmodule
