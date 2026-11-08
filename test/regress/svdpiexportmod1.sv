// DPI-C export of a module-scope function, called from SystemVerilog.
// Expected values: IEEE 1800-2017 35.7 (export declarations), 13.4 (function call), 21.2.
module svdpiexportmod1;
  function int sv_inc(int x); return x + 1; endfunction
  export "DPI-C" function sv_inc;
  initial begin
    if (sv_inc(7) !== 8) $error("sv_inc(7) = %0d", sv_inc(7));
    else $display("PASSED");
  end
endmodule
