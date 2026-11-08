// IEEE 1800-2017 21.2.1.3: without a field width an integral value is
// shown with as many digits as its width needs.  A class handle has no
// width (8.4); nvc formats its object number like a 32-bit unsigned
// value, so %h gives eight digits, %b 32 and %d ten characters, and a
// zero field width gives the fewest digits as for any other value.
module svhandlefmt2;
  class C; endclass
  C c;
  int errors = 0;

  task check(string got, int len, string what);
    if (got.len() != len) begin
      $display("%s: '%s' has %0d characters, expected %0d", what, got,
               got.len(), len);
      errors++;
    end
  endtask

  initial begin
    string dec;
    c = new;
    check($sformatf("%h", c), 8, "%h");
    check($sformatf("%x", c), 8, "%x");
    check($sformatf("%b", c), 32, "%b");
    check($sformatf("%o", c), 11, "%o");
    check($sformatf("%d", c), 10, "%d");
    dec = $sformatf("%0d", c);
    if ($sformatf("%0h", c) != $sformatf("%0h", dec.atoi())) begin
      $display("%%0h and %%0d do not agree");
      errors++;
    end
    if (errors == 0) $display("PASSED");
  end
endmodule
