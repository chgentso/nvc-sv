// $fatal ends the simulation as $finish does (so final procedures run); two final procedures in one module.
// Expected values: IEEE 1800-2017 9.2.3 (final), 20.10 ($fatal calls $finish), 20.2 ($finish runs final
// procedures), 9.4.1 (delay: n=1 at time 5). The order of the two final procedures and the $fatal text are
// implementation-defined; a counter prints PASSED after the second one. This test must fail (the $fatal is deliberate).
module svfinal4;
  int n = 0;
  int done = 0;
  int errs = 0;
  initial begin #5 n = 1; $fatal(1, "fatal here"); end
  final begin
    if (n !== 1) begin $error("n must be 1, got %0d", n); errs++; end
    done++;
    if (done == 2 && errs == 0) $display("PASSED");
  end
  final begin
    done++;
    if (done == 2 && errs == 0) $display("PASSED");
  end
endmodule
