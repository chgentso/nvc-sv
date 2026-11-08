// IEEE 1800-2017 16.9.3: $sampled returns the sampled value of its argument,
// the value in the Preponed region of the current time step (16.5.1).  The
// action block of a concurrent assertion runs later in the same time step
// (Reactive region, 16.14.1, 4.4.2), so $sampled there gives the values the
// assertion used, while a plain reference gives the current value.
`timescale 1ns/1ns
module svsampledact1;
  bit clk, a, b;
  int got_sampled = -1, got_current = -1, nfail, errs;

  always #5 clk = ~clk;                 // Edges at 5, 15, ...
  always @(posedge clk) a = b;          // a follows b at the edge

  assert property (@(posedge clk) a == b)
    else begin
      nfail++;
      got_sampled = $sampled(a);
      got_current = a;
    end

  initial begin
    #12 b = 1;                          // At 15: sampled a=0, b=1
    #10;
    if (nfail != 1) begin $error("%0d failures", nfail); errs++; end
    if (got_sampled != 0) begin $error("$sampled(a) = %0d", got_sampled); errs++; end
    if (got_current != 1) begin $error("a = %0d", got_current); errs++; end
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
