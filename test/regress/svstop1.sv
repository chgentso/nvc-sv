// Expected values: IEEE 1800-2017 20.2 ($stop suspends the simulation at the current time; the argument 1
// requests a diagnostic message), 21.2 ($display), 9.2.3 (final procedures run when the simulation ends).
// What happens after $stop (the simulator may wait at an interactive prompt, resume or end) is not fixed by
// the LRM, so statements after the $stop and the later initial block are not checked; the checks are that
// the code before $stop ran at 10 ns and that nothing ran after 10 ns before the stop.
`timescale 1ns/1ns
module svstop1;
  int before_t = -1;
  int bad_early = 0;
  initial begin
    #10;
    $display("before stop");
    before_t = $time;
    $stop(1);
    $display("after stop");
  end
  initial begin
    #20;
    $display("not reached");
  end
  initial begin
    #5;
    if (before_t != -1) bad_early = 1;
  end
  final begin
    if (before_t != 10) $error("code before $stop did not run at 10 ns (before_t=%0d)", before_t);
    else if (bad_early) $error("process ran out of order");
    else $display("PASSED");
  end
endmodule
