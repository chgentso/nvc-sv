// $error inside a final procedure does not stop it; $finish at 47 runs the final procedure.
// Expected values: IEEE 1800-2017 9.2.3 (final), 9.4.2 (posedges of clk at 5,15,25,35,45 give n=5),
// 20.2 ($finish runs final procedures), 20.10 ($error is a non-fatal severity task: execution continues),
// 21.2 ($display, %0t). The $strobe output from a final procedure and the $error text are
// implementation-defined and are not checked. This test must fail (the $error is deliberate).
`timescale 1ns/1ns
module svfinal2;
  int n = 0;
  int errs = 0;
  bit last_seen = 0;
  bit after_error = 0;
  logic clk = 0;
  always #5 clk = ~clk;
  initial #47 $finish;
  always @(posedge clk) n++;
  initial #42 last_seen = 1;
  final begin
    if (n !== 5) begin $error("n must be 5 at the end, got %0d", n); errs++; end
    if ($time != 47) begin $error("$time in final must be 47, got %0t", $time); errs++; end
    if (!last_seen) begin $error("the #42 process did not run"); errs++; end
    $strobe("strobe in final");
    $error("error in final");
    after_error = 1;
    if (!after_error) errs++;
    if (errs == 0) $display("PASSED");
  end
endmodule
