// IEEE 1800-2017 21.2.1.8: %s shows its argument as a sequence of 8-bit
// ASCII codes, and an enumeration is an integral type (6.19), so %s of an
// enum prints the characters of its value.  A literal format string
// gives a warning pointing at name() and %p, which print the label.
module svenumfmt2;
  typedef enum {RED, GREEN, BLUE=32'h41424344} c_t;
  c_t c = BLUE;
  string s;
  initial begin
    s = $sformatf("%s|%p|%s", c, c, c.name());
    if (s == "ABCD|BLUE|BLUE")
      $display("PASSED");
    else
      $display("FAILED %s", s);
  end
endmodule
