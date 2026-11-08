// IEEE 1800-2017 16.4: each execution of a deferred immediate assertion
// whose result needs an action places a report in the deferred assertion
// report queue of its process, with the values of the action block
// arguments at that execution (16.4.2), so an assertion evaluated several
// times in one time step reports each failure.  16.4.4: when the process
// resumes after a suspension (a flush point) its pending reports are
// discarded, so a combinational glitch does not report.  The action blocks
// are single subroutine calls as 16.4 requires.
`timescale 1ns/1ns
module svdeferq1;
  logic a = 0, b = 0;
  int i, j, glitch, errs;
  bit seen_o[3], seen_f[3];
  int n_o, n_f;

  function void note_o(int k); seen_o[k] = 1; n_o++; endfunction
  function void note_f(int k); seen_f[k] = 1; n_f++; endfunction
  function void bump(); glitch++; endfunction

  // Fails at t=1 before b follows a (flushed), and at t=2 when a=0, b=1
  always_comb begin
    assert #0 (a == b) else bump();
  end

  initial begin
    for (i = 0; i < 3; i++)
      assert #0 (i < 0) else note_o(i);
    for (j = 0; j < 3; j++)
      assert final (j < 0) else note_f(j);
    #1 a = 1;
    #0 b = 1;
    #1 a = 0;
    #1 b = 0;
    #1;
    if (n_o != 3 || !seen_o[0] || !seen_o[1] || !seen_o[2]) begin
      $error("observed deferred: %0d reports %p", n_o, seen_o);
      errs++;
    end
    if (n_f != 3 || !seen_f[0] || !seen_f[1] || !seen_f[2]) begin
      $error("final deferred: %0d reports %p", n_f, seen_f);
      errs++;
    end
    if (glitch != 1) begin
      $error("always_comb assertion reported %0d failures", glitch);
      errs++;
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
