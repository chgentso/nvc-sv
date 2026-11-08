// Expected values: IEEE 1800-2017 26.3 (a local declaration of a name that has not yet been referenced takes
// precedence over a wildcard-imported candidate), 26.2 (pkg::name always refers to the package item).
package pb1; int y = 11; endpackage
module svimpdup2;
  import pb1::*;
  int y = 5;
  int err = 0;
  initial begin
    if (y != 5) begin err++; $error("y=%0d expected 5", y); end
    if (pb1::y != 11) begin err++; $error("pb1::y=%0d expected 11", pb1::y); end
    if (err == 0) $display("PASSED");
  end
endmodule
