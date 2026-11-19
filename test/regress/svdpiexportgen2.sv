// C code calls a DPI export declared in the active branch of an if-generate,
// selecting the scope of the generate block by name (IEEE 1800-2017 35.5.4,
// 35.9); the same name is exported by the other branch in another instance.
module leaf #(parameter bit ALT = 0);
  int state = 10;
  if (ALT) begin : g_alt
    export "DPI-C" function sv_bump;
    function automatic int sv_bump(int delta);
      state += 2 * delta;
      return state;
    endfunction
  end else begin : g_std
    export "DPI-C" function sv_bump;
    function automatic int sv_bump(int delta);
      state += delta;
      return state;
    endfunction
  end

  import "DPI-C" context function int dpi_call_in(string path, int delta);
  initial begin
    #1 $display("%m %0d", dpi_call_in(ALT ? "svdpiexportgen2.u1.g_alt"
                                      : "svdpiexportgen2.u0.g_std", 3));
  end
endmodule

module svdpiexportgen2;
  leaf #(.ALT(0)) u0();
  leaf #(.ALT(1)) u1();
endmodule
