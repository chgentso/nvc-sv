// Expected values: IEEE 1800-2017 9.4.3, 9.7, 14.3, 16.4.4 (clocking blocks,
// initial clock generators after fork/join, suspended tasks, and deferred
// assertions preserve O(1) sv_watched skipping and exact wake/flush behavior).
`timescale 1ns/1ns
module svpollskip2;
  class Box;
    int cnt = 0;
    int susp_val = 0;
    bit go = 0;
  endclass

  Box c;
  logic clk = 0;
  logic sig = 0;
  logic out_sig = 0;
  logic [7:0] rtl_q = 8'h00;
  int rare_flag = 0;
  int woke_cnt = 0;
  int woke_susp = 0;
  int woke_mix = 0;
  int defer_reports = 0;
  int flush_reports = 0;
  process p_hold, p_wait_susp;

  function void bump_defer(); defer_reports++; endfunction
  function void bump_flush(); flush_reports++; endfunction
  function void rare_fn(); c.cnt += 100; endfunction

  clocking cb @(posedge clk);
    input rtl_q;
    output out_sig;
  endclocking

  clocking cb_empty @(negedge clk);
  endclocking

  // Mirrors clk_rst_if clock generator: fork/join at t=0, cold branch, then forever loop
  initial begin
    bit ready = 0;
    fork
      begin
        #1;
        ready = 1;
      end
    join
    if (!ready) $fatal(1, "fork/join failed");
    forever begin
      if (rare_flag) rare_fn();
      #5 clk = 1'b1;
      #5 clk = 1'b0;
    end
  end

  always_ff @(posedge clk) begin
    rtl_q <= rtl_q + 8'h01;
  end

  // Deferred assertion alongside wait() in the same time step
  bit trig_defer = 0;
  bit defer_gate = 0;
  always @(posedge trig_defer) begin
    assert #0 (0) else bump_defer();
    wait (defer_gate);
  end

  bit trig_flush = 0;
  always @(posedge trig_flush) begin
    assert #0 (0) else bump_flush();
    wait (defer_gate);
  end

  initial begin
    c = new;

    fork
      begin
        p_hold = process::self();
        p_hold.suspend();
      end
      begin
        p_wait_susp = process::self();
        wait (c.susp_val == 5);
        woke_susp++;
      end
      begin
        wait (c.cnt == 42);
        woke_cnt++;
      end
      begin
        wait (sig == 1'b1 && c.go == 1'b1);
        woke_mix++;
      end
    join_none

    #2;
    if (p_hold.status() != process::SUSPENDED)
      $fatal(1, "expected p_hold SUSPENDED");

    // Also suspend a process currently waiting on sv_watched
    p_wait_susp.suspend();
    #1;
    if (p_wait_susp.status() != process::SUSPENDED)
      $fatal(1, "expected p_wait_susp SUSPENDED");

    // 10k cycles with clocking blocks + suspended tasks + post-fork clock gen
    repeat (10000) @(posedge clk);
    if (woke_cnt != 0 || woke_susp != 0 || woke_mix != 0)
      $fatal(1, "premature wake: %0d %0d %0d", woke_cnt, woke_susp, woke_mix);

    // Trigger deferred assertion while waiting in sv_watched across delta cycles
    #1 trig_defer = 1'b1;
    #0;
    #0;
    #1;
    if (defer_reports != 1)
      $fatal(1, "defer_reports=%0d expected 1", defer_reports);
    defer_gate = 1'b1;
    #1 defer_gate = 1'b0;

    // Resume in the same time step flushes the pending deferred assertion
    #1 trig_flush = 1'b1;
    #0 defer_gate = 1'b1;
    #1;
    if (flush_reports != 0)
      $fatal(1, "flush_reports=%0d expected 0", flush_reports);

    // Class writes must still wake immediately
    #1 c.cnt = 42;
    #1;
    if (woke_cnt != 1)
      $fatal(1, "woke_cnt=%0d expected 1", woke_cnt);

    // Suspended poller wakes only after resume() once condition holds
    c.susp_val = 5;
    #1;
    if (woke_susp != 0)
      $fatal(1, "suspended poller woke before resume");
    p_wait_susp.resume();
    #1;
    if (woke_susp != 1)
      $fatal(1, "woke_susp=%0d expected 1 after resume", woke_susp);

    // Mixed signal + class cond still wakes
    c.go = 1'b1;
    #1 sig = 1'b1;
    #1;
    if (woke_mix != 1)
      $fatal(1, "woke_mix=%0d expected 1", woke_mix);

    p_hold.kill();
    $display("PASSED");
    $finish;
  end
endmodule
