// IEEE 1800-2017 6.12.1 does not define the conversion of a NaN or an
// infinity to an integral type.  nvc gives all x for a four-state type
// and zero for a two-state type with a warning, and $rtoi likewise
// returns x, the four-state unknown value.
module svnanint1;
  real r, i;
  logic [7:0] l;
  logic [99:0] w;
  integer k;
  int n;
  bit [7:0] b;
  int err = 0;
  initial begin
    r = 0.0 / 0.0;
    i = 1.0 / 0.0;
    l = r; w = i; k = -i; n = r; b = i;
    if (l !== 8'bx) err++;
    if (w !== 100'bx) err++;
    if (k !== 32'bx) err++;
    if (n !== 0) err++;
    if (b !== 0) err++;
    if ($rtoi(r) !== 32'bx) err++;
    l = 2.5;
    if (l !== 3) err++;
    if (err == 0) $display("PASSED");
    else $display("FAILED %0d", err);
  end
endmodule
