// Expected values: IEEE 1800-2017 26.3 (wildcard imports: a name is imported only if referenced and not locally
// declared; names that are candidates from two packages are an error only when referenced; imported names are not
// re-exported by a wildcard import in another package), 26.2 (scope-qualified reference pkg::name), 6.20.
package pa1; int x = 1; int y = 11; parameter int W = 5; endpackage
package pa2; int x = 2; parameter int W = 6; endpackage
package pa3; import pa1::*; import pa2::*; int u = 3; endpackage
module svimpdup1;
  import pa1::*;
  import pa2::*;
  import pa3::*;
  int y2 = 0;
  int err = 0;
  initial begin
    if (y != 11) begin err++; $error("y=%0d expected 11", y); end
    if (u != 3) begin err++; $error("u=%0d expected 3", u); end
    if (pa1::x != 1) begin err++; $error("pa1::x=%0d", pa1::x); end
    if (pa2::W != 6) begin err++; $error("pa2::W=%0d", pa2::W); end
    if (pa1::W != 5 || pa2::x != 2) begin err++; $error("qualified references"); end
    if (err == 0) $display("PASSED");
  end
endmodule
