// A function that calls $fatal on a path which is not taken by the
// simulation but is when the function is called to compute the value of a
// parameter. Evaluating the parameter while elaborating must not end the
// elaboration with a crash; the message is shown as a warning and the value
// the function builds is used.
package svfatalconst1_pkg;
  function static bit [3:0] conv(bit [3:0] val);
    conv = 0;
    for (int i = 0; i < 4; i++) begin
      if (val[i] == 1'b0) conv[i] = 1'b0;
      else if (val[i] == 1'b1 && i != 3) conv[i] = 1'b1;
      else $fatal(0, "bad bit %0d of %b", i, val);
    end
  endfunction

  parameter bit [3:0] P = conv(4'b1000);
endpackage

module svfatalconst1;
  import svfatalconst1_pkg::*;
  initial begin
    if (P !== 4'b0000) $fatal(1, "P = %b", P);
    $display("PASSED");
  end
endmodule
