// %p and %s with widths and flags applied to enums, reals, strings and integers
// Expected values: IEEE 1800-2017 21.2.1.7 (%p prints the value as an assignment pattern: an enum prints its
// label), 21.2.1.8 (%s: the argument is a sequence of 8-bit ASCII codes, so an enum prints its value as
// characters, not its label), 21.2.1.2/21.2.1.3 (%0d, %x, field widths), 6.19 (enum values).
// Layout (padding, zero fill, precision of %p) is not specified by 21.2.1.7, so %p results are checked
// by looking for the content (padding may be blanks or zeros); for %s only the last character (the value's low byte) and the length are checked.
module svfmt3;
  typedef enum {RED, GREEN, BLUE} color_t;
  typedef enum logic [1:0] {A0=0, A1=1, A2=2, A3=3} l_t;
  color_t c;
  real r;
  string s;
  int i;
  l_t l;
  int errs = 0;
  function automatic string nows(string x);
    string o = "";
    foreach (x[k]) if (x[k] != " ") o = {o, x[k]};
    return o;
  endfunction
  function automatic bit has(string x, string sub);
    for (int k = 0; k + sub.len() <= x.len(); k++)
      if (x.substr(k, k + sub.len() - 1) == sub) return 1;
    return 0;
  endfunction
  task automatic eq(string got, string exp, string m);
    if (got != exp) begin $error("%s: got '%s' expected '%s'", m, got, exp); errs++; end
  endtask
  task automatic lastc(string got, int code, int len, string m);
    if (got.len() != len || got.len() == 0 || got[got.len()-1] != code) begin
      $error("%s: got len %0d last %0d, expected len %0d last %0d", m, got.len(),
             got.len() ? got[got.len()-1] : -1, len, code);
      errs++;
    end
  endtask
  initial begin
    string t;
    i = 42; c = BLUE; r = 3.5; s = "hi"; l = A3;
    if (!has($sformatf("%10p", c), "BLUE")) begin $error("%%10p enum"); errs++; end
    if (!has($sformatf("%-10p", c), "BLUE")) begin $error("%%-10p enum"); errs++; end
    eq(nows($sformatf("%3p", c)), "BLUE", "%3p enum");
    eq(nows($sformatf("%0p", c)), "BLUE", "%0p enum");
    if (nows($sformatf("%10p", r)).atoreal() != 3.5) begin $error("%%10p real"); errs++; end
    if (nows($sformatf("%-10p", r)).atoreal() != 3.5) begin $error("%%-10p real"); errs++; end
    if (nows($sformatf("%10.3p", r)).atoreal() != 3.5) begin $error("%%10.3p real"); errs++; end
    if (!has($sformatf("%10p", s), "hi")) begin $error("%%10p string"); errs++; end
    if (!has($sformatf("%-10p", s), "hi")) begin $error("%%-10p string"); errs++; end
    eq(nows($sformatf("%p", l)), "A3", "%p l_t");
    eq(nows($sformatf("%p", l_t'(3))), "A3", "%p l_t'(3)");
    eq(nows($sformatf("%p", color_t'(1))), "GREEN", "%p color_t'(1)");
    // %s of an enum is ASCII of the 32-bit value (default base type int)
    lastc($sformatf("%s", BLUE), 2, 4, "%s BLUE");
    lastc($sformatf("%s", color_t'(1)), 1, 4, "%s color_t'(1)");
    lastc($sformatf("%s", c), 2, 4, "%s c");
    lastc($sformatf("%0s", c), 2, 1, "%0s c");
    lastc($sformatf("%6s", c), 2, 6, "%6s c");
    lastc($sformatf("%5s", color_t'(2)), 2, 5, "%5s color_t'(2)");
    lastc($sformatf("%s", color_t'(7)), 7, 4, "%s color_t'(7)");
    eq($sformatf("%10s", s), "        hi", "%10s string");
    t = $sformatf("%010s", s);
    if (t.len() != 10 || nows(t) != "hi") begin $error("%%010s string"); errs++; end
    eq($sformatf("%0d", color_t'(2)), "2", "%0d color_t'(2)");
    t = $sformatf("%p", color_t'(7));
    if (has(t, "RED") || has(t, "GREEN") || has(t, "BLUE")) begin $error("%%p of an out of range enum value must not be a label: %s", t); errs++; end
    if (!has($sformatf("%8p", 42), "42")) begin $error("%%8p int"); errs++; end
    if (!has($sformatf("%-8p", 42), "42")) begin $error("%%-8p int"); errs++; end
    if (!has($sformatf("%-10p", i), "42")) begin $error("%%-10p int"); errs++; end
    if (!has($sformatf("%010p", i), "42")) begin $error("%%010p int"); errs++; end
    t = nows($sformatf("%p", 8'hA5));
    if (t != "165" && t != "8'ha5" && t != "'ha5" && t != "a5" && t != "A5") begin $error("%%p 8'hA5 got %s", t); errs++; end
    eq(nows($sformatf("%p %p", c, color_t'(2))), "BLUEBLUE", "%p %p");
    eq($sformatf("%x", c), "00000002", "%x enum");
    if (errs == 0) $display("PASSED");
  end
endmodule
