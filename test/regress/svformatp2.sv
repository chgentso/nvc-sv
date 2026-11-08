// IEEE 1800-2017 21.2.1.3: a field width pads the value on the left with
// spaces, for example %5d.  21.2.1.7 leaves the exact text of %p to the tool;
// nvc prints an integral value for %p as for %d, so a field width pads it with
// spaces in the same way, and also pads an enumeration name, a real value and a
// string with spaces.
module svformatp2;
  typedef enum {RED, GRN} c_t;
  c_t c = GRN;
  real r = 1.5;
  string s = "ab";
  int errs;

  task automatic eq(string got, string exp);
    if (got != exp) begin
      $error("got [%s], expected [%s]", got, exp);
      errs++;
    end
  endtask

  initial begin
    eq($sformatf("%5p", 8'd3), $sformatf("%5d", 8'd3));
    eq($sformatf("%5p", 8'd3), "    3");
    eq($sformatf("%7p", -3), "     -3");
    eq($sformatf("%-5p|", 8'd3), "3    |");
    eq($sformatf("%5p", c), "  GRN");
    eq($sformatf("%6p", r), "   1.5");
    eq($sformatf("%6p", s), "  \"ab\"");
    if (errs == 0) $display("PASSED");
  end
endmodule
