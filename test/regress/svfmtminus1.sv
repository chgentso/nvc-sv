// Format specifiers combined with field width, '-' and '0' flags for %x %d %b
// %o %h %s %c %t %e %f %g, including values with x/z digits.
// Expected values: IEEE 1800-2017 21.2.1.2 (format specifications: %0d/%0h/%0b
// give the minimum width, default field widths of %d %h %b %o are derived from
// the size of the operand, x/z digit rules for %h %b %o), 21.2.1.5 (%c, %s),
// 21.2.1.2 (real formats follow C), 20.4.2 ($timeformat, %t).
// The LRM does not define the '-' flag or the zero-fill flag of an explicit
// field width, nor what an explicit width does to a result: for those formats
// only the LRM-defined content is checked (the digits that are printed, after
// removing blanks and zero fill); exact text is checked only where the LRM
// determines it (natural width, %0<fmt>, C-style real formats).  For %0h/%0b/%0o
// of operands with x/z digits, whether a zero digit next to an x/z digit and
// repeated leading x/z digits are kept is not specified; there only the
// trailing digits are checked.  The letter case of hex digits is not checked.
timeunit 1ns; timeprecision 1ps;
module svfmtminus1;
  logic [31:0] v;
  logic [7:0] b;
  logic [15:0] x;
  int n;
  logic [7:0] y;
  real r;
  int errors = 0;

  // strip blanks around the text, then zero fill after an optional sign
  function automatic string norm(string s);
    int lo = 0, hi = s.len() - 1;
    string sign = "";
    while (lo <= hi && s.substr(lo, lo) == " ") lo++;
    while (hi >= lo && s.substr(hi, hi) == " ") hi--;
    if (lo > hi) return "";
    if (s.substr(lo, lo) == "-" && hi > lo) begin sign = "-"; lo++; end
    while (lo < hi && s.substr(lo, lo) == "0") lo++;
    return {sign, s.substr(lo, hi)};
  endfunction
  task automatic chkn(string fmt, string got, string canon);   // content only
    if (norm(got) != canon) begin
      $error("format %s: got '%s', digits '%s' expected '%s'", fmt, got, norm(got), canon); errors++;
    end
  endtask
  task automatic chke(string fmt, string got, string exp);     // exact text
    if (got != exp) begin
      $error("format %s: got '%s' expected '%s'", fmt, got, exp); errors++;
    end
  endtask
  task automatic chki(string fmt, string got, string canon);   // content, letter case ignored
    string g = norm(got), c = canon;
    if (g.tolower() != c.tolower()) begin
      $error("format %s: got '%s' expected digits '%s' (case ignored)", fmt, got, canon); errors++;
    end
  endtask
  task automatic chks(string fmt, string got, string suffix);  // digits end with suffix
    string g = norm(got);
    if (g.len() < suffix.len() || g.substr(g.len() - suffix.len(), g.len() - 1) != suffix) begin
      $error("format %s: got '%s', digits do not end with '%s'", fmt, got, suffix); errors++;
    end
  endtask
`define I(F, A, C) begin string r_; r_ = $sformatf(F, A); chki(F, r_, C); end
`define S(F, A, C) begin string r_; r_ = $sformatf(F, A); chks(F, r_, C); end
`define N(F, A, C) begin string r_; r_ = $sformatf(F, A); chkn(F, r_, C); end
`define E(F, A, X) begin string r_; r_ = $sformatf(F, A); chke(F, r_, X); end

  initial begin
    v = 12; b = 5; n = -7; x = 16'h00zx;
    // natural width: 8 hex digits, 10 decimal digits (unsigned 32 bit), 11 octal digits
    `E("%x", v, "0000000c")
    `I("%X", 32'hab, "ab")
    begin string r_; r_ = $sformatf("%X", 32'hab); if (r_.len() != 8) begin $error("%%X natural width %0d", r_.len()); errors++; end end
    `E("%h", v, "0000000c")
    `E("%d", v, "        12")
    `E("%b", b, "00000101")
    `E("%o", v, "00000000014")
    `E("%d", n, "         -7")
    // minimum width
    `E("%0x", v, "c")
    `E("%0d", v, "12")
    `E("%0h", v, "c")
    `E("%0b", b, "101")
    `E("%0o", v, "14")
    `E("%0d", n, "-7")
    // explicit width, '-' and '0' flags: digits only
    `N("%-08x", v, "c")
    `N("%-8x", v, "c")
    `N("%08x", v, "c")
    `N("%8x", v, "c")
    `N("%-2x", v, "c")
    `N("%-0x", v, "c")
    `N("%-x", v, "c")
    `I("%-08X", 32'hab, "ab")
    `I("%-8X", 32'hab, "ab")
    `I("%08X", 32'hab, "ab")
    `I("%8X", 32'hab, "ab")
    `N("%-08d", v, "12")
    `N("%-8d", v, "12")
    `N("%08d", v, "12")
    `N("%8d", v, "12")
    `N("%-2d", v, "12")
    `N("%-0d", v, "12")
    `N("%-d", v, "12")
    `N("%-08d", n, "-7")
    `N("%-8d", n, "-7")
    `N("%08d", n, "-7")
    `N("%8d", n, "-7")
    `N("%-08b", b, "101")
    `N("%-8b", b, "101")
    `N("%08b", b, "101")
    `N("%8b", b, "101")
    `N("%-0b", b, "101")
    `N("%-b", b, "101")
    `N("%-08o", v, "14")
    `N("%-8o", v, "14")
    `N("%08o", v, "14")
    `N("%8o", v, "14")
    `N("%-0o", v, "14")
    `N("%-o", v, "14")
    `N("%-08h", v, "c")
    `N("%-8h", v, "c")
    `N("%08h", v, "c")
    `N("%8h", v, "c")
    `N("%-0h", v, "c")
    `N("%-h", v, "c")
    // 4-state operands: digit rules of %h (a nibble that is all x / all z is x / z)
    `E("%h", x, "00zx")
    `N("%-8h", x, "zx")
    `N("%08h", x, "zx")
    `N("%-8x", x, "zx")
    `N("%-1h", x, "zx")
    `N("%-3h", x, "zx")
    `N("%-5h", x, "zx")
    x = 16'h0000;
    `E("%h", x, "0000")
    `E("%0h", x, "0")
    `E("%0d", x, "0")
    `E("%0b", x, "0")
    `N("%-0h", x, "0")
    `N("%-8h", x, "0")
    `N("%-1h", x, "0")
    `N("%-3h", x, "0")
    `N("%-3d", x, "0")
    `N("%-3b", x, "0")
    `N("%-8b", x, "0")
    x = 16'h0x0f;
    `E("%h", x, "0x0f")
    `E("%b", x, "0000xxxx00001111")
    `N("%-0h", x, "x0f")
    `N("%-8h", x, "x0f")
    `N("%-1h", x, "x0f")
    `N("%-3h", x, "x0f")
    `N("%-8b", x, "xxxx00001111")
    x = 16'hzz00;
    `E("%h", x, "zz00")
    `S("%0h", x, "z00")
    `S("%-0h", x, "z00")
    `S("%-8h", x, "z00")
    y = 8'b0000x011;
    `E("%b", y, "0000x011")
    `S("%0b", y, "x011")
    `S("%-0b", y, "x011")
    `S("%-12b", y, "x011")
    `S("%-1b", y, "x011")
    `S("%-3b", y, "x011")
    y = 8'b0000_0z11;
    `E("%o", y, "00Z")        // octal digit with z and 1 bits is Z
    `S("%0o", y, "Z")
    y = 8'bxxxxxxxx;
    `E("%h", y, "xx")
    `E("%0h", y, "x")
    `E("%b", y, "xxxxxxxx")
    `E("%0b", y, "x")
    `E("%d", y, "  x")
    `E("%0d", y, "x")
    `N("%-0h", y, "x")
    `N("%-6h", y, "x")
    // strings and characters
    `E("%s", "ab", "ab")
    `E("%0s", "ab", "ab")
    `N("%-4s", "ab", "ab")
    `N("%4s", "ab", "ab")
    `N("%-04s", "ab", "ab")
    `N("%04s", "ab", "ab")
    `N("%-0s", "ab", "ab")
    `N("%-s", "ab", "ab")
    `N("%5s", "ab", "ab")
    `N("%-5s", "ab", "ab")
    `N("%05s", "ab", "ab")
    `N("%-05s", "ab", "ab")
    `E("%c", 8'h41, "A")
    `E("%0c", 8'h41, "A")
    `N("%-4c", 8'h41, "A")
    `N("%4c", 8'h41, "A")
    `N("%-04c", 8'h41, "A")
    `N("%-c", 8'h41, "A")
    `N("%3c", 8'h41, "A")
    `N("%-3c", 8'h41, "A")
    `N("%03c", 8'h41, "A")
    // reals: formats follow C
    r = 3.14159;
    `E("%e", r, "3.141590e+00")
    `E("%f", r, "3.141590")
    `E("%g", r, "3.14159")
    `E("%.2f", r, "3.14")
    `E("%.3f", r, "3.142")
    `E("%8.2f", r, "    3.14")
    `N("%-8.2f", r, "3.14")
    `N("%-08.2f", r, "3.14")
    `N("%08.2f", r, "3.14")
    `N("%-12e", r, "3.141590e+00")
    `N("%012e", r, "3.141590e+00")
    `N("%-12g", r, "3.14159")
    `N("%012g", r, "3.14159")
    `N("%-12f", r, "3.141590")
    `N("%012f", r, "3.141590")
    `N("%-12.2f", r, "3.14")
    `N("%012.2f", r, "3.14")
    `N("%12.2f", r, "3.14")
    `N("%-.3f", r, "3.142")
    `N("%-012.3f", r, "3.142")
    // signed operands with explicit width
    `N("%-5d", 8'sd5, "5")
    `N("%-05d", 8'sd5, "5")
    `N("%-5d", -8'sd5, "-5")
    `N("%-5d", 3'sd2, "2")
    // combined format
    begin
      string r_;
      r_ = $sformatf("%s0x%-08x", "-", 255);
      if (r_.len() < 5 || r_.substr(0, 2) != "-0x") begin $error("combined: '%s'", r_); errors++; end
      else chkn("combined", r_.substr(3, r_.len() - 1), "ff");
    end
    // time: printed in ns with no decimals after $timeformat
    $timeformat(-9, 0, "", 1);
    `E("%t", $time, "0")
    `N("%-10t", $time, "0")
    `N("%10t", $time, "0")
    `N("%-010t", $time, "0")
    `E("%0t", $time, "0")
    #1;
    `E("%0t", $time, "1")
    `E("%t", $time, "1")
    `N("%-t", $time, "1")
    `N("%-10t", $time, "1")
    `N("%10t", $time, "1")
    `N("%010t", $time, "1")
    if (errors == 0) $display("PASSED");
  end
endmodule
