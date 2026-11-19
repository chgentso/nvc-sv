// A DPI export declared in a generate block next to the function it exports
// (IEEE 1800-2017 35.5.4).
module svdpiexportgen1 #(parameter bit ICache = 0);
  if (ICache) begin : g_ic
    logic x;
  end else begin : g_no_ic
    export "DPI-C" function get_key;
    function automatic int get_key();
      return 5;
    endfunction
  end
  initial $display("%0d", g_no_ic.get_key());
endmodule
