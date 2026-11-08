// Backslash-newline inside string literals (including macro actuals and macro bodies) is dropped.
// Expected values: IEEE 1800-2017 5.9 (strings: "\" newline is ignored), 22.5.1 (macro text/actuals)
string got;
`define show(MSG) got = MSG
`define twoline "def\
   ghi"
module svppstrcont1;
  int errs = 0;
  initial begin
    `show("This has more than one field.\
          Please use the method instead.");
    if (got != "This has more than one field.          Please use the method instead.") begin
      errs++; $error("s1 '%s'", got);
    end
    `show("a\
b\
c");
    if (got != "abc") begin errs++; $error("s2 '%s'", got); end
    got = `twoline;
    if (got != "def   ghi") begin errs++; $error("s3 '%s'", got); end
    `show("plain");
    if (got != "plain") begin errs++; $error("s4 '%s'", got); end
    if (errs == 0) $display("PASSED");
  end
endmodule
