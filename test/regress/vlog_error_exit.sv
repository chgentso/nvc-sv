// $error reports an error but does not end the simulation (20.10), so the
// statement after it still runs; the test is expected to fail (error severity).
// Expected values: IEEE 1800-2017 20.10
module vlog_error_exit;
  bit reached;
  initial begin
    #1 $error("boom %0d", 5);
    #1 $display("after");
    reached = 1;
  end
  final if (!reached) $fatal(1, "simulation ended after $error");
endmodule
