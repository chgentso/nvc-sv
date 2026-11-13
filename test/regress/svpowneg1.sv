// Self-checking. IEEE 1800-2017 11.4.3 table 11-4: a negative exponent gives
// zero for a base beyond one, one for a base of one, minus one or one for a
// base of minus one and unknown for a base of zero. The exponent is
// self-determined so its sign does not depend on the base.
module svpowneg1;
  integer i = -2, j = -3;
  reg [3:0] b = 15;
  reg signed [3:0] s = -2, m1 = -1;
  reg signed [7:0] sb = -3;
  bit [7:0] z = 0;
  bit signed [7:0] e = -1;
  bit [7:0] r;
  initial begin
    if (15 ** -2 !== 0) $error("FAIL a");
    if ((b ** i) !== 0) $error("FAIL b %0d", b ** i);
    if ((b ** s) !== 0) $error("FAIL c");
    if ((m1 ** s) !== 4'sd1) $error("FAIL d");
    if ((m1 ** j) !== -4'sd1) $error("FAIL e %0d", m1 ** j);
    if (1 ** -5 !== 1) $error("FAIL f");
    if ((0 ** i) !== 32'bx) $error("FAIL g");
    if ((sb ** s) !== 8'sd0) $error("FAIL h");
    if (3 ** 2 !== 9) $error("FAIL i");
    r = z ** e;
    if (r !== 8'h00) $error("FAIL two-state %h", r);
    $display("PASSED");
  end
endmodule
