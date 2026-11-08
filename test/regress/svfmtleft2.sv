// Left justified fields in format strings
// Expected values: IEEE 1800-2017 21.2.1.2/21.2.1.3: a minus sign after % left-justifies the value in its field
// (widths: %b of 8 bits is 8 characters; explicit widths pad with blanks; %0d is minimal width).
// The default width of %-d (21.2.1.3: large enough for the biggest value of the type) is checked only as "5 then blanks".
// The combination of '-' with a leading zero (%-02d) is not defined precisely by the LRM, so only the
// digit and the absence of other characters are checked.
module svfmtleft2;
  reg [7:0] r = 8'hab;
  real x = 3.5;
  int errs = 0;
  task automatic eq(string got, string exp, string m);
    if (got != exp) begin $error("%s: got '%s' expected '%s'", m, got, exp); errs++; end
  endtask
  function automatic string nows(string x);
    string o = "";
    foreach (x[k]) if (x[k] != " ") o = {o, x[k]};
    return o;
  endfunction
  initial begin
    string t;
    $display("[%-d][%-4d][%-8b][%-4h][%-8.2f][%-3c][%-5s]", 5, 5, r, r, x, 65, "ab");
    t = $sformatf("%-d", 5);
    if (t.len() < 1 || t[0] != "5" || nows(t) != "5") begin $error("%%-d: got '%s'", t); errs++; end
    eq($sformatf("%-4d", 5), "5   ", "%-4d");
    eq($sformatf("%-8b", r), "10101011", "%-8b");
    eq($sformatf("%-4h", r), "ab  ", "%-4h");
    eq($sformatf("%-8.2f", x), "3.50    ", "%-8.2f");
    eq($sformatf("%-3c", 65), "A  ", "%-3c");
    eq($sformatf("%-5s", "ab"), "ab   ", "%-5s");
    $display("[%-0d][%-02d]", 5, 5);
    eq($sformatf("%-0d", 5), "5", "%-0d");
    t = $sformatf("%-02d", 5);
    if (nows(t) != "5" || t[0] != "5") begin $error("%%-02d: got '%s'", t); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
