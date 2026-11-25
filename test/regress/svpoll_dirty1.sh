#!/bin/bash -ex
set -xe

if [ -n "${ASAN_OPTIONS:-}" ]; then
  export ASAN_OPTIONS="$ASAN_OPTIONS:detect_leaks=0"
fi

cat > svpoll_dirty1.sv <<'EOF'
// Stress test for event-driven sv_poll_end_cycle dirty list:
// 1. Many idle signal-only pollers + @* alongside a fast toggling clock
// 2. Exact wait_seq wake ordering across multiple signals and shared signal
// 3. Multi-signal pollers, mixed signal + class-member pollers
// 4. Forked / disabled / killed pollers (including same-delta signal toggle)
// 5. Packed 4-state signal and RTL island output triggering wait() pollers

module island_gen (
  input  logic        clk,
  input  logic        rst_n,
  input  logic        en,
  output logic [31:0] cnt_o
);
  logic [31:0] cnt_q, cnt_d;

  always_comb begin
    cnt_d = en ? (cnt_q + 32'h0001_0001) : cnt_q;
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)
      cnt_q <= 32'h0;
    else
      cnt_q <= cnt_d;
  end

  assign cnt_o = cnt_q;
endmodule

module svpoll_dirty1;
  class Box;
    int  val = 0;
    bit  go  = 0;
  endclass

  localparam int N_IDLE = 64;

  logic        clk = 1'b0;
  logic        rst_n = 1'b0;
  logic        isl_en = 1'b0;
  logic [31:0] isl_cnt;

  logic [N_IDLE-1:0] idle_sig = '0;
  logic        comb_obs;
  logic [1:0]  ord_a = 2'b00, ord_b = 2'b00, ord_shared = 2'b00;
  logic [3:0]  ms_a = 4'h0, ms_b = 4'h0;
  logic        mix_sig = 1'b0;
  logic        kill_sig = 1'b0;
  logic [31:0] wide_sig = 32'h0;
  int          dyn_bit = 0;

  Box box;

  always #5 clk = ~clk;

  always @* begin
    comb_obs = ^idle_sig;
  end

  island_gen u_isl (
    .clk   (clk),
    .rst_n (rst_n),
    .en    (isl_en),
    .cnt_o (isl_cnt)
  );

  int idle_woken = 0;
  int idle_last_id = -1;

  int ord_log[16];
  int ord_n = 0;

  int multi_woken = 0;
  int mix_woken = 0;
  int kill_woken = 0;
  int wide_woken = 0;
  int isl_woken = 0;

  process p_kill;

  initial begin
    box = new;

    // 1. Spawn 64 idle signal-only pollers
    for (int i = 0; i < N_IDLE; i++) begin
      automatic int id = i;
      fork
        begin
          wait (idle_sig[id] == 1'b1);
          idle_woken++;
          idle_last_id = id;
        end
      join_none
    end

    // 2. Spawn polled signal-only waiters in deterministic wait_seq order
    //    (indexing with a variable forces sv_poll_wait + sv_poll_watch_sig):
    //    IDs 1,2,3 wait on ord_a / ord_b / ord_a (we will toggle ord_b BEFORE ord_a in same delta!)
    fork
      begin
        automatic int b = dyn_bit;
        wait (ord_a[b] == 1'b1);
        ord_log[ord_n++] = 1;
      end
    join_none
    #0;
    fork
      begin
        automatic int b = dyn_bit;
        wait (ord_b[b] == 1'b1);
        ord_log[ord_n++] = 2;
      end
    join_none
    #0;
    fork
      begin
        automatic int b = dyn_bit;
        wait (ord_a[b] == 1'b1);
        ord_log[ord_n++] = 3;
      end
    join_none
    #0;

    //    Shared-signal wait_seq ordering (4, 5, 6, 7)
    for (int k = 4; k <= 7; k++) begin
      automatic int kid = k;
      automatic int b = dyn_bit;
      fork
        begin
          wait (ord_shared[b] == 1'b1);
          ord_log[ord_n++] = kid;
        end
      join_none
      #0;
    end

    // 3. Multi-signal poller, mixed signal+class poller, packed 32-bit 4-state poller, island poller
    fork
      begin
        wait (ms_a == 4'h5 && ms_b == 4'ha);
        multi_woken++;
      end
      begin
        wait (mix_sig == 1'b1 && box.go == 1'b1 && box.val == 77);
        mix_woken++;
      end
      begin
        wait (wide_sig === 32'hcafe_babe);
        wide_woken++;
      end
      begin
        wait (isl_cnt == 32'h0005_0005);
        isl_woken++;
      end
    join_none

    // 4. Forked poller that will be killed (even after its signal toggles in same delta)
    fork
      begin
        p_kill = process::self();
        wait (kill_sig == 1'b1);
        kill_woken++;
      end
    join_none

    // Release reset and run 2000 fast clock cycles with periodic memory writes
    #12 rst_n = 1'b1;
    for (int c = 0; c < 2000; c++) begin
      @(posedge clk);
      if ((c & 63) == 0)
        box.val = c & 15; // Triggers MACRO_MEM_DIRTY without waking signal-only pollers
    end

    if (idle_woken != 0 || ord_n != 0 || multi_woken != 0 || mix_woken != 0
        || wide_woken != 0 || isl_woken != 0 || kill_woken != 0)
      $fatal(1, "premature wake: idle=%0d ord=%0d multi=%0d mix=%0d wide=%0d isl=%0d kill=%0d",
             idle_woken, ord_n, multi_woken, mix_woken, wide_woken, isl_woken, kill_woken);

    // Wake exactly one idle signal-only poller (#37)
    #1 idle_sig[37] = 1'b1;
    #1;
    if (idle_woken != 1 || idle_last_id != 37 || comb_obs !== 1'b1)
      $fatal(1, "idle wake mismatch: woken=%0d id=%0d comb=%b",
             idle_woken, idle_last_id, comb_obs);

    // Toggle ord_b BEFORE ord_a in the SAME delta: must wake in wait_seq order 1, 2, 3!
    #1;
    ord_b[0] = 1'b1;
    ord_a[0] = 1'b1;
    #1;
    if (ord_n != 3 || ord_log[0] != 1 || ord_log[1] != 2 || ord_log[2] != 3)
      $fatal(1, "cross-signal wait_seq mismatch: n=%0d log=%0d,%0d,%0d",
             ord_n, ord_log[0], ord_log[1], ord_log[2]);

    // Toggle shared signal: must wake in wait_seq order 4, 5, 6, 7!
    #1 ord_shared[0] = 1'b1;
    #1;
    if (ord_n != 7 || ord_log[3] != 4 || ord_log[4] != 5
        || ord_log[5] != 6 || ord_log[6] != 7)
      $fatal(1, "shared-signal wait_seq mismatch: n=%0d log=%0d,%0d,%0d,%0d",
             ord_n, ord_log[3], ord_log[4], ord_log[5], ord_log[6]);

    // Multi-signal poller: only ms_a first (must not wake), then ms_b
    #1 ms_a = 4'h5;
    #1;
    if (multi_woken != 0)
      $fatal(1, "multi_woken fired on ms_a only");
    #1 ms_b = 4'ha;
    #1;
    if (multi_woken != 1)
      $fatal(1, "multi_woken=%0d expected 1", multi_woken);

    // Mixed signal + class-member poller
    #1 mix_sig = 1'b1;
    #1;
    if (mix_woken != 0)
      $fatal(1, "mix_woken fired with box.go==0");
    #1 mix_sig = 1'b0;
    #1 box.go = 1'b1;
    box.val = 77;
    #1;
    if (mix_woken != 0)
      $fatal(1, "mix_woken fired with mix_sig==0");
    #1 mix_sig = 1'b1;
    #1;
    if (mix_woken != 1)
      $fatal(1, "mix_woken=%0d expected 1", mix_woken);

    // Kill poller in the same delta as toggling its watched signal
    #1;
    kill_sig = 1'b1;
    p_kill.kill();
    #1;
    if (kill_woken != 0)
      $fatal(1, "killed poller woke up");

    // Also test disable fork on a freshly parked poller
    fork
      begin
        fork
          begin
            wait (idle_sig[0] == 1'b1);
            kill_woken++;
          end
        join_none
        #1;
        disable fork;
      end
    join
    #1 idle_sig[0] = 1'b1;
    #1;
    if (kill_woken != 0 || idle_woken != 2)
      $fatal(1, "disable fork failed: kill=%0d idle=%0d", kill_woken, idle_woken);

    // Packed 32-bit 4-state signal (16-byte watch snapshot)
    #1 wide_sig = 32'hdead_beef;
    #1;
    if (wide_woken != 0)
      $fatal(1, "wide_woken fired on wrong value");
    #1 wide_sig = 32'hcafe_babe;
    #1;
    if (wide_woken != 1)
      $fatal(1, "wide_woken=%0d expected 1", wide_woken);

    // RTL island output driving a signal-only poller
    @(negedge clk);
    isl_en = 1'b1;
    repeat (6) @(posedge clk);
    #1;
    if (isl_woken != 1)
      $fatal(1, "isl_woken=%0d expected 1 (isl_cnt=%08h)", isl_woken, isl_cnt);

    $display("PASSED");
    $finish;
  end
endmodule
EOF

nvc --work=work_off -a svpoll_dirty1.sv
nvc --work=work_off --rtl-islands=off -e svpoll_dirty1
nvc --work=work_off -r svpoll_dirty1 | grep -q "^PASSED$"

nvc --work=work_hier -a svpoll_dirty1.sv
nvc --work=work_hier --rtl-islands=hier -e svpoll_dirty1
nvc --work=work_hier -r svpoll_dirty1 | grep -q "^PASSED$"
