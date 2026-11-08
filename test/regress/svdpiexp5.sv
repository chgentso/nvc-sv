// A DPI import not declared context calls an exported function.
// IEEE 1800-2017 35.5.3: only a context import may call exported
// subroutines.  The call is made from C, so it can only be detected when it
// happens: it is a run-time error.
module svdpiexp5;
  export "DPI-C" function sv_nc_target;

  function int sv_nc_target(int a);
    return a + 1;
  endfunction

  import "DPI-C" function int dpi_noctx_call(int a);   // not context

  int r;

  initial begin
    r = dpi_noctx_call(1);
    $display("FAILED: the call returned %0d", r);
  end
endmodule
