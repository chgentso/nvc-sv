// Disabling a process while its imported context task is blocked in an
// exported task.
// IEEE 1800-2017 35.9: the exported task returns 1 when the process is
// disabled, svIsDisabledState() is then nonzero, the imported task calls
// svAckDisabledState() and returns 1, and the statements after the call are
// not executed; an exported task that completes normally returns 0 and
// svIsDisabledState() is 0.  Annex H.9 (svIsDisabledState,
// svAckDisabledState).
// C side: test/dpi/dpi_export2.c (dpi_blocking, dpi_dis_log).
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
module svdpiexp3;
  export "DPI-C" task sv_block;
  import "DPI-C" context task dpi_blocking();
  import "DPI-C" function int dpi_dis_log(input int which);

  event go;
  int reached = 0, normal = 0;

  task sv_block();
    @go;
  endtask

  int err = 0;

  initial begin
    fork
      begin : blk
        dpi_blocking();
        reached = 1;
      end
    join_none
    #5 disable blk;
    #1;
    `CHK(reached == 0, "statement after the disabled call not executed")
    // 100 * export return + 10 * svIsDisabledState(); the last digit (state
    // after svAckDisabledState) is not specified and is ignored
    `CHK(dpi_dis_log(0) / 10 == 11, "disabled: export returned 1, state set")

    fork
      begin
        dpi_blocking();
        normal = 1;
      end
    join_none
    #1 ->go;
    #1;
    `CHK(normal == 1, "normal completion")
    `CHK(dpi_dis_log(1) == 0, "not disabled: export returned 0, state clear")

    if (err == 0) $display("PASSED");
  end
endmodule
