// $printtimescale in a nested hierarchy: every instance of one module inherits the unit and
// precision of the enclosing `timescale. (The default time unit without `timescale is
// implementation-defined, so a timescale is given explicitly.)
// Expected values: IEEE 1800-2017 20.4 ($printtimescale/$timeunit/$timeprecision), 22.7 (`timescale)
`timescale 10ns/1ns
package pk;
  int n = 0;
  int errs = 0;
endpackage
module leaf;
  initial begin
    $printtimescale;
    pk::n++;
    if ($timeunit != -8 || $timeprecision != -9) begin pk::errs++; $error("%m: unit %0d precision %0d", $timeunit, $timeprecision); end
  end
endmodule

module mid;
  leaf l1();
  leaf l2();
endmodule

module svprinttscale1;
  mid m();
  initial begin
    #1;
    if (pk::n != 2) begin pk::errs++; $error("%0d leaves ran", pk::n); end
    if (pk::errs == 0) $display("PASSED");
  end
endmodule
