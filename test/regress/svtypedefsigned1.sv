// Expected values: IEEE 1800-2017 6.18 (typedef), 6.11.3 (signed integer types), 11.8.1 (signed comparison and
// arithmetic), 21.2.1 (%d prints signed values)
module svtypedefsigned1;
  typedef bit signed [63:0] wide_t;
  typedef bit signed [3:0] nib_t;
  typedef int int_t;
  wide_t g;
  int errs;
  string st;
  initial begin
    wide_t l;
    nib_t n;
    int_t i;
    g = -8; l = -8; n = -3; i = -2;
    if ((g < 0) !== 1'b1 || (l < 0) !== 1'b1 || (n < 0) !== 1'b1 || (i < 0) !== 1'b1) begin $error("signed compare"); errs++; end
    st = $sformatf("%0d %0d %0d %0d", g, l, n, i);
    if (st != "-8 -8 -3 -2") begin $error("values '%s'", st); errs++; end
    st = $sformatf("%0d", l + 1);
    if (st != "-7") begin $error("l+1 '%s'", st); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
