// Test hierarchical references to generate block labels that shadow visible typedefs

package svhiertype1_pkg;
  typedef integer SCOPE;
endpackage

typedef int G;

module svhiertype1;
  import svhiertype1_pkg::*;

  if (1) begin : G
    localparam int VALUE = 7;
  end

  initial begin
    SCOPE.val = 17;
    if (G.VALUE !== 7) $fatal(1, "G.VALUE mismatch: %0d", G.VALUE);
    if (SCOPE.val !== 17) $fatal(1, "SCOPE.val mismatch: %0d", SCOPE.val);
    $display("PASSED");
    $finish;
  end

  generate
    if (1) begin : SCOPE
      integer val;
    end
  endgenerate
endmodule
