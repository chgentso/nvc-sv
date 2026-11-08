// Deferred immediate assertions in a procedure disabled by another process.
// IEEE 1800-2017 16.4.4: disabling the outermost scope of a procedure flushes
// its deferred assertion report queue, so the reports of the evaluations made
// before it suspended are dropped; 9.6.2: disabling a named block terminates
// the execution of that block in the process executing it (here suspended in
// a delay control), and an always procedure then starts again, waiting on its
// event control.
`timescale 1ns/1ns
module svdeferdisable3;
  string reps[$];
  function void rep(string id, int v);
    reps.push_back($sformatf("%0t:%s:%0d", $time, id, v));
  endfunction

  int ta = 0, after = 0;
  bit dis_a = 0;
  event go_a;
  always @(ta) begin : b1
    a1: assert #0 (ta == 0) else rep("a1", ta);
    -> go_a;
    #5 after++;
  end
  always @go_a if (dis_a) disable b1;

  initial begin
    #10 dis_a = 1; ta = 1;      // Report flushed by the disable
    #2  dis_a = 0; ta = 2;      // Reported; the delay completes at 17
  end

  initial begin
    automatic string exp[$] = '{"12:a1:2"};
    #100;
    if (reps != exp || after != 1) begin
      $display("reports %p after %0d", reps, after);
      $display("expected %p after 1", exp);
      $error("mismatch");
    end
    else
      $display("PASSED");
  end
endmodule
