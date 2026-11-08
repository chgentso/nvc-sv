// Null characters in the %s argument of a display or sformatf
// Expected values: IEEE 1800-2017 6.16 (a string holds no null characters: zero bytes are ignored when an
// integral value is assigned to a string), 11.4.14 (streaming into a string), 21.2.1.8 (%s: leading zero
// bytes print as blanks, %0s drops them, an embedded zero byte is not printed as NUL), 21.3 ($sformatf).
// How a zero byte inside (not leading) a %s argument is shown (blank or dropped) is not fixed here, so those results are
// only checked for containing no NUL and for the visible characters.
module svfmtnul1;
  string s, t, u;
  bit [31:0] w = 32'h41004243;
  int errs = 0;
  task automatic eq(string got, string exp, string m);
    if (got != exp) begin $error("%s: got '%s' (len %0d) expected '%s'", m, got, got.len(), exp); errs++; end
  endtask
  function automatic bit nonul(string x);
    foreach (x[k]) if (x[k] == 0) return 0;
    return 1;
  endfunction
  initial begin
    string x;
    s = $sformatf("a%sb", 24'h410042);
    $display("len=%0d", s.len());
    $display(s);
    $display("[%s]", s);
    if (!nonul(s) || s[0] != "a" || s[1] != "A" || s[s.len()-1] != "b" || s[s.len()-2] != "B" || (s.len() != 4 && s.len() != 5)) begin
      $error("$sformatf a%%sb of A,NUL,B: got len %0d", s.len()); errs++;
    end
    {>>{u}} = w;
    $display("len=%0d", u.len());
    $display(u);
    $display("[%s]", u);
    eq(u, "ABC", "streaming 41004243 into a string drops the zero byte");
    $display("[%s] [%s]", 32'h41004243, 24'h004142);
    x = $sformatf("[%s]", 24'h004142);
    eq(x, "[ AB]", "leading zero byte of a 3 byte %s prints as a blank");
    x = $sformatf("%s", 32'h41004243);
    if (!nonul(x) || x[0] != "A" || x[x.len()-1] != "C" || x[x.len()-2] != "B" || (x.len() != 3 && x.len() != 4)) begin
      $error("%%s of 41004243: got len %0d", x.len()); errs++;
    end
    $display("[%0s]", 32'h00004142);
    eq($sformatf("[%0s]", 32'h00004142), "[AB]", "%0s drops leading zero bytes");
    t = $sformatf("<%s>,%0d", u, 7);
    $display("tlen=%0d", t.len());
    $display(t);
    $display("%s|", t);
    $display("<%s>", u);
    $display("%s|%s", u, "tail");
    $display("%s|", $sformatf("%s", u));
    eq(t, "<ABC>,7", "$sformatf with string u");
    eq($sformatf("%s|%s", u, "tail"), "ABC|tail", "two %s");
    eq($sformatf("%s", u), "ABC", "%s of string");
    if (errs == 0) $display("PASSED");
  end
endmodule
