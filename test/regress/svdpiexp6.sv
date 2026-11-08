// An imported DPI function (context) calls an exported task.
// IEEE 1800-2017 35.5.2 / 35.7 (only imported tasks may call exported
// tasks) and 13.4 (a function shall not enable a task).  The call is made
// from C, so it can only be detected when it happens: it is a run-time
// error.
module svdpiexp6;
  export "DPI-C" task sv_ftask;

  task sv_ftask(input int a);
    $display("FAILED: the exported task ran with %0d", a);
  endtask

  import "DPI-C" context function int dpi_func_calls_task(int a);

  int r;

  initial begin
    r = dpi_func_calls_task(3);
    $display("FAILED: the call returned %0d", r);
  end
endmodule
