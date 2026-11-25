// Test lazy MIR lowering triggering constant-function folding via
// jit_call_thunk2 during jit_fill_irbuf (must not self-deadlock on j->lock).
package vlog_jit_fold_reentry1_pkg;
  function automatic int compute_words(int bytes);
    int r = bytes / 4;
    if (r < 1)
      r = 1;
    return r;
  endfunction
endpackage

interface vlog_jit_fold_reentry1_if #(
  parameter int SizeBytes = 64
);
  localparam int NumWords = vlog_jit_fold_reentry1_pkg::compute_words(SizeBytes);
  logic [31:0] data;

  function automatic int get_words();
    return NumWords;
  endfunction
endinterface

module vlog_jit_fold_reentry1;
  vlog_jit_fold_reentry1_if #(.SizeBytes(128)) u_if();

  function automatic int check_vif(virtual vlog_jit_fold_reentry1_if #(.SizeBytes(128)) vif);
    return vif.get_words();
  endfunction

  initial begin
    int w;
    w = check_vif(u_if);
    if (w !== 32)
      $fatal(1, "expected 32 words, got %0d", w);
    $display("PASSED");
    $finish;
  end
endmodule
