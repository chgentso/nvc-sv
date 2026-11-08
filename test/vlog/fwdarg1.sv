// A name other than a hierarchical name must be declared before it is
// used (IEEE 1800-2017 23.6 allows forward references only for
// hierarchical names).  nvc accepts a variable declared later as an
// argument of a system task as an extension, with a warning.
module fwdarg1;
  initial begin
    #1 $display("%0d", later_sig);      // Warning
    $display("%m");                     // OK
  end
  logic [3:0] later_sig = 5;
  initial $display("%0d", later_sig);   // OK
endmodule
