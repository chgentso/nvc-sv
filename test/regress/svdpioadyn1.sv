// A dynamic array actual for an unsized output formal of a DPI import, run
// with --relaxed. IEEE 1800-2017 7.7 makes this illegal (the callee cannot
// size the array) so it is only a warning in relaxed mode; the elements the
// C code writes (35.5.6.1, Annex H: svSize gives the current size of the
// dynamic array) are visible in the actual after the call.
// C side: test/dpi/dpi_openarr.c, oa_fill_int: element p becomes base + p * p.
module svdpioadyn1;
  import "DPI-C" function void oa_fill_int(output int x[], input int base);
  int dyn[];
  int err = 0;
  initial begin
    dyn = new[5];
    oa_fill_int(dyn, 10);
    if (dyn.size() != 5) begin err++; $error("size %0d", dyn.size()); end
    for (int p = 0; p < 5; p++)
      if (dyn[p] != 10 + p * p) begin err++; $error("dyn[%0d] = %0d", p, dyn[p]); end
    if (err == 0) $display("PASSED");
  end
endmodule
