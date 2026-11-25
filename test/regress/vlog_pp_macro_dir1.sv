`define DEFIF_A_OR_NOT_B(A, B, C) \
  `undef C \
  `ifdef A \
    `define C \
  `endif \
  `ifndef B \
    `define C \
  `endif

`define DEFIF_NOT_A_OR_B(A, B, C) `DEFIF_A_OR_NOT_B(B, A, C)

`define FLAG_SET 1
`DEFIF_NOT_A_OR_B(UNSET_A, UNSET_B, SHOULD_BE_DEF)
`DEFIF_NOT_A_OR_B(FLAG_SET, UNSET_B, SHOULD_BE_UNDEF)

module vlog_pp_macro_dir1;
  int ok1 = 0, ok2 = 1;
`ifdef SHOULD_BE_DEF
  initial ok1 = 1;
`endif
`ifdef SHOULD_BE_UNDEF
  initial ok2 = 0;
`endif

  initial begin
    #1;
    if (ok1 != 1 || ok2 != 1)
      $fatal(1, "ok1=%0d ok2=%0d", ok1, ok2);
    $display("PASSED");
    $finish;
  end
endmodule
