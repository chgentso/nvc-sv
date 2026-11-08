// Constrained-random testbench for a small synchronous FIFO (valid/ready both sides).
// DUT: sync_fifo (parameter AW, depth 2**AW) with pass-through write when full and popping.

`timescale 1ns/1ps

module sync_fifo #(parameter AW = 4) (
  input             clk,
  input             rst_n,
  input             wr_valid,
  output            wr_ready,
  input      [7:0]  wr_data,
  output            rd_valid,
  input             rd_ready,
  output     [7:0]  rd_data,
  output     [AW:0] count,
  output            almost_full
);
  localparam DEPTH = 1 << AW;
  reg [7:0]  mem [0:DEPTH-1];
  reg [AW-1:0] wptr;
  reg [AW-1:0] rptr;
  reg [AW:0]   cnt;
  wire full  = (cnt == DEPTH);
  wire empty = (cnt == 0);
  assign rd_valid    = !empty;
  assign wr_ready    = !full || rd_ready;   // accept a write on a full FIFO if it is popped this cycle
  wire do_wr = wr_valid && wr_ready;
  wire do_rd = rd_valid && rd_ready;
  assign rd_data     = mem[rptr];
  assign count       = cnt;
  assign almost_full = (cnt >= DEPTH - 2);
  always @(posedge clk) begin
    if (!rst_n) begin
      wptr <= 0;
      rptr <= 0;
      cnt  <= 0;
    end else begin
      if (do_wr) begin
        mem[wptr] <= wr_data;
        wptr <= wptr + 1'b1;
      end
      if (do_rd) rptr <= rptr + 1'b1;
      cnt <= cnt + do_wr - do_rd;
    end
  end
endmodule

interface fifo_if (input logic clk);
  logic       rst_n;
  logic       wr_valid;
  logic       wr_ready;
  logic [7:0] wr_data;
  logic       rd_valid;
  logic       rd_ready;
  logic [7:0] rd_data;
  logic [7:0] count;
  logic       almost_full;
endinterface

module crv_tb;
  localparam AW = 4;
  localparam DEPTH = 1 << AW;

  typedef enum {K_SINGLE, K_SHORT, K_LONG, K_RAMP} kind_e;

  // ---------------------------------------------------------------- Transaction
  class Transaction;
    static int next_id;
    int id;
    rand kind_e kind;
    rand int len;
    rand bit [7:0] data [];
    rand int gap [];
    rand int rd_pct;
    rand bit back2back;
    rand bit [3:0] tag_a, tag_b;
    int nzeros, nff, sum;

    constraint c_kind { kind dist { K_SINGLE := 2, K_SHORT := 5, K_LONG := 2, K_RAMP := 1 }; }
    constraint c_len {
      solve kind before len;
      if (kind == K_SINGLE) len == 1;
      else if (kind == K_SHORT) len inside {[2:6]};
      else if (kind == K_LONG) len inside {[7:20]};
      else len inside {[4:12]};
    }
    constraint c_size { data.size() == len; gap.size() == len; }
    constraint c_data {
      if (kind == K_RAMP) {
        data[0] < 200;
        foreach (data[i]) if (i > 0) data[i] == data[i-1] + 1;
      } else {
        foreach (data[i]) data[i] dist { 0 := 2, [1:254] :/ 16, 255 := 2 };
      }
    }
    constraint c_gap {
      foreach (gap[i]) gap[i] inside {[0:6]};
      foreach (gap[i]) if (back2back) gap[i] == 0;
      else gap[i] dist { 0 := 6, [1:3] :/ 3, [4:6] :/ 1 };
    }
    constraint c_pct { rd_pct dist { 10 := 1, 30 := 1, 50 := 2, 80 := 3, 100 := 3 }; }
    constraint c_b2b { soft back2back == 0; }
    constraint c_tags { unique {tag_a, tag_b}; }

    function void post_randomize();
      nzeros = 0; nff = 0; sum = 0;
      foreach (data[i]) begin
        if (data[i] == 0) nzeros++;
        if (data[i] == 255) nff++;
        sum += data[i];
      end
      id = next_id;
      next_id++;
    endfunction
  endclass

  // ---------------------------------------------------------------- Config shared by driver and sink
  class Cfg;
    int rd_pct = 100;
  endclass

  // ---------------------------------------------------------------- Generator
  class Generator;
    mailbox #(Transaction) mbx;
    int n_gen, n_fail;
    int kind_cnt [4];
    int len1, len_short, len_mid, len_long;
    int b2b_total, b2b_in_phase0, ramp_beats;
    int long_ge_maxlen_phase4;

    task automatic run_phase(int phase, int n);
      Transaction tr;
      int maxlen;
      int ok;
      maxlen = 6;
      for (int k = 0; k < n; k++) begin
        tr = new();
        case (phase)
          0: ok = tr.randomize();
          1: ok = tr.randomize() with { rd_pct <= 30; kind inside {K_SHORT, K_LONG}; };
          2: ok = tr.randomize() with { rd_pct == 100; back2back == 1; };
          3: ok = tr.randomize() with { kind == K_RAMP; len > 8; rd_pct >= 50; };
          default: ok = tr.randomize() with { len <= maxlen; rd_pct != 100; };
        endcase
        if (ok != 1) n_fail++;
        n_gen++;
        kind_cnt[tr.kind]++;
        if (tr.len == 1) len1++;
        else if (tr.len <= 6) len_short++;
        else if (tr.len <= 12) len_mid++;
        else len_long++;
        if (tr.back2back) begin
          b2b_total++;
          if (phase == 0) b2b_in_phase0++;
        end
        if (tr.kind == K_RAMP) ramp_beats += tr.len;
        if (phase == 4 && tr.len > maxlen) long_ge_maxlen_phase4++;
        mbx.put(tr);
      end
    endtask
  endclass

  // ---------------------------------------------------------------- Driver
  class Driver;
    virtual fifo_if vif;
    mailbox #(Transaction) mbx;
    Cfg cfg;
    int done, beats;

    task run();
      Transaction tr;
      forever begin
        mbx.get(tr);
        cfg.rd_pct = tr.rd_pct;
        for (int i = 0; i < tr.len; i++) begin
          repeat (tr.gap[i]) @(posedge vif.clk);
          vif.wr_valid <= 1'b1;
          vif.wr_data  <= tr.data[i];
          @(posedge vif.clk);
          while (!vif.wr_ready) @(posedge vif.clk);
          vif.wr_valid <= 1'b0;
          beats++;
        end
        done++;
      end
    endtask
  endclass

  // ---------------------------------------------------------------- Sink (read side driver)
  class Sink;
    virtual fifo_if vif;
    Cfg cfg;
    task run();
      forever begin
        @(posedge vif.clk);
        vif.rd_ready <= ($urandom_range(99) < cfg.rd_pct);
      end
    endtask
  endclass

  // ---------------------------------------------------------------- Monitor
  class Monitor;
    virtual fifo_if vif;
    mailbox #(bit [7:0]) mbx_in;
    mailbox #(bit [7:0]) mbx_out;
    int wr_stall, rd_stall, full_cyc, empty_rdy_cyc, passthru, almost_cyc, max_cnt;
    int inv_errors;
    int cyc;

    task run();
      forever begin
        @(posedge vif.clk);
        if (vif.rst_n === 1'b1) begin
          cyc++;
          if (vif.wr_valid && vif.wr_ready) mbx_in.put(vif.wr_data);
          if (vif.rd_valid && vif.rd_ready) mbx_out.put(vif.rd_data);
          if (vif.wr_valid && !vif.wr_ready) wr_stall++;
          if (vif.rd_valid && !vif.rd_ready) rd_stall++;
          if (vif.count == DEPTH) full_cyc++;
          if (vif.rd_ready && !vif.rd_valid) empty_rdy_cyc++;
          if (vif.count == DEPTH && vif.wr_valid && vif.wr_ready) passthru++;
          if (vif.almost_full) almost_cyc++;
          if (vif.count > max_cnt) max_cnt = vif.count;
          // interface invariants
          if (vif.rd_valid !== (vif.count != 0)) inv_errors++;
          if (vif.wr_ready !== (vif.count != DEPTH || vif.rd_ready)) inv_errors++;
          if (vif.almost_full !== (vif.count >= DEPTH - 2)) inv_errors++;
          if (vif.count > DEPTH) inv_errors++;
        end
      end
    endtask
  endclass

  // ---------------------------------------------------------------- Scoreboard
  class Scoreboard;
    mailbox #(bit [7:0]) mbx_in;
    mailbox #(bit [7:0]) mbx_out;
    bit [7:0] exp [$];
    bit [7:0] bin, bout, e;
    int n_in, n_out, errors;

    task run();
      fork
        forever begin
          mbx_in.get(bin);
          exp.push_back(bin);
          n_in++;
        end
        forever begin
          mbx_out.get(bout);
          n_out++;
          if (exp.size() == 0) begin
            errors++;
            if (errors < 5) $display("SB: output %0h with nothing expected", bout);
          end else begin
            e = exp.pop_front();
            if (e !== bout) begin
              errors++;
              if (errors < 5) $display("SB: mismatch exp %0h got %0h at out #%0d", e, bout, n_out);
            end
          end
        end
      join_none
    endtask
  endclass

  // ---------------------------------------------------------------- Environment
  logic clk = 1'b0;
  always #5 clk = ~clk;
  fifo_if fif (clk);

  wire [AW:0] cnt_w;
  always @* fif.count = cnt_w;

  sync_fifo #(.AW(AW)) dut (
    .clk(clk), .rst_n(fif.rst_n),
    .wr_valid(fif.wr_valid), .wr_ready(fif.wr_ready), .wr_data(fif.wr_data),
    .rd_valid(fif.rd_valid), .rd_ready(fif.rd_ready), .rd_data(fif.rd_data),
    .count(cnt_w), .almost_full(fif.almost_full));

  Generator  gen;
  Driver     drv;
  Sink       snk;
  Monitor    mon;
  Scoreboard sb;
  Cfg        cfg;
  mailbox #(Transaction) gen2drv;
  mailbox #(bit [7:0])   mon2sb_in;
  mailbox #(bit [7:0])   mon2sb_out;
  int errors;

  task automatic fail(string msg);
    errors++;
    $display("CHECK: %s", msg);
  endtask

  task automatic check_min(string name, int got, int minv);
    if (got < minv) fail({name, " below threshold"});
    if (got < minv) $display("  %s = %0d, need >= %0d", name, got, minv);
  endtask

  task automatic main_flow();
    gen.run_phase(0, 1200);
    while (!(drv.done == gen.n_gen)) @(posedge clk);
    gen.run_phase(1, 500);
    while (!(drv.done == gen.n_gen)) @(posedge clk);
    gen.run_phase(2, 800);
    while (!(drv.done == gen.n_gen)) @(posedge clk);
    gen.run_phase(3, 300);
    while (!(drv.done == gen.n_gen)) @(posedge clk);
    gen.run_phase(4, 600);
    while (!(drv.done == gen.n_gen)) @(posedge clk);
    // drain
    cfg.rd_pct = 100;
    while (!(fif.count == 0 && sb.exp.size() == 0)) @(posedge clk);
    repeat (10) @(posedge clk);
  endtask

  initial begin
    gen2drv = new(4);
    mon2sb_in = new();
    mon2sb_out = new();
    cfg = new();
    gen = new(); gen.mbx = gen2drv;
    drv = new(); drv.mbx = gen2drv; drv.vif = fif; drv.cfg = cfg;
    snk = new(); snk.vif = fif; snk.cfg = cfg;
    mon = new(); mon.vif = fif; mon.mbx_in = mon2sb_in; mon.mbx_out = mon2sb_out;
    sb  = new(); sb.mbx_in = mon2sb_in; sb.mbx_out = mon2sb_out;

    fif.rst_n = 1'b0; fif.wr_valid = 1'b0; fif.wr_data = 8'h00; fif.rd_ready = 1'b0;
    repeat (4) @(posedge clk);
    fif.rst_n <= 1'b1;
    @(posedge clk);

    fork
      begin
        fork
          drv.run();
          snk.run();
          mon.run();
          sb.run();
        join_none
        main_flow();
      end
      begin
        #40ms;
        fail("timeout");
      end
    join_any
    disable fork;

    // ------------------------------------------------ end-of-test checks
    if (sb.errors != 0) fail("scoreboard errors");
    if (mon.inv_errors != 0) fail("interface invariant errors");
    if (gen.n_fail != 0) fail("randomize() failures");
    if (sb.n_in != drv.beats) fail("monitor missed input beats");
    if (sb.n_out != sb.n_in) fail("input/output beat count differ");
    if (sb.exp.size() != 0) fail("data left in expected queue");
    if (fif.count != 0) fail("DUT not empty at end");
    if (gen.n_gen < 3000) fail("too few transactions");
    if (gen.b2b_in_phase0 != 0) fail("soft back2back==0 violated in phase 0");
    if (gen.long_ge_maxlen_phase4 != 0) fail("inline local maxlen violated");
    // simple functional coverage with minimum thresholds
    check_min("kind SINGLE", gen.kind_cnt[K_SINGLE], 50);
    check_min("kind SHORT", gen.kind_cnt[K_SHORT], 200);
    check_min("kind LONG", gen.kind_cnt[K_LONG], 100);
    check_min("kind RAMP", gen.kind_cnt[K_RAMP], 100);
    check_min("len 1", gen.len1, 100);
    check_min("len 2..6", gen.len_short, 200);
    check_min("len 7..12", gen.len_mid, 100);
    check_min("len 13..20", gen.len_long, 20);
    check_min("b2b transactions", gen.b2b_total, 500);
    check_min("ramp beats", gen.ramp_beats, 1000);
    check_min("write stall cycles", mon.wr_stall, 200);
    check_min("read backpressure cycles", mon.rd_stall, 200);
    check_min("full cycles", mon.full_cyc, 50);
    check_min("empty with ready cycles", mon.empty_rdy_cyc, 200);
    check_min("almost_full cycles", mon.almost_cyc, 50);
    check_min("pass-through writes", mon.passthru, 5);
    check_min("total beats", sb.n_out, 15000);
    if (mon.max_cnt != DEPTH) fail("FIFO never reached DEPTH");

    if (errors == 0) $display("PASSED");
    else $display("FAILED %0d check(s)", errors);
    $finish;
  end
endmodule
