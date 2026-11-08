// expect statements with a sequence property, implication with a range
// delay, repetition, disable iff, throughout on the opposite clock edge,
// until_with and a constant property, reached at a clock edge or between
// edges.
// Expected values: IEEE 1800-2017 16.17 (expect: the process blocks until
// the property succeeds or fails; evaluation starts at the first clock tick
// strictly after the statement is reached - an expect reached at a clock
// edge starts at the next edge; the statement completes at the tick that
// decides the property), 16.9.1 (##), 16.9.2 ([*]), 16.12 (implication, until_with,
// throughout 16.9.9), 16.9.3 (sampling at ticks).
// A reference model records the sampled values at every posedge and replays
// the chain of expect statements on them: for each statement the start
// tick, the deciding tick (= time of the reported result) and the result.
// The model's try(id, result, time) list must equal the observed one.  An expect
// whose disable condition is true at its first tick has no property result; what
// it does then is not specified, it is only required that the following
// statements start at the tick after it (it is assumed to complete at that tick).
module svaexpect1;
  logic clk = 0;
  logic a = 0, b = 0, c = 0, rst = 0;
  logic [7:0] d = 0;
  logic [15:0] lfsr = 16'hBEEF;
  always #5 clk = ~clk;
  always @(negedge clk) begin
    lfsr <= {lfsr[14:0], lfsr[15] ^ lfsr[13] ^ lfsr[12] ^ lfsr[10]};
    a <= lfsr[0] | lfsr[3];
    b <= lfsr[1] & lfsr[4];
    c <= lfsr[2];
    d <= lfsr[10:3];
    rst <= lfsr[7] & lfsr[8] & lfsr[9];
  end

  // sampled values at posedge ticks k (time 5 + 10 k)
  bit pa[$], pb[$], pc[$], prst[$];
  always @(posedge clk) begin
    pa.push_back(a); pb.push_back(b); pc.push_back(c); prst.push_back(rst);
  end

  int errors = 0;
  int npass, nfail;
  int o_id[$], o_res[$], o_t[$];   // observed
  task automatic try(int id, bit r);
    if (r) npass++; else nfail++;
    o_id.push_back(id); o_res.push_back(r); o_t.push_back($time);
  endtask

  task automatic t1;
    expect (@(posedge clk) a ##1 b) try(1, 1); else try(1, 0);
  endtask

  initial begin
    // statement reached at the clock edge itself
    @(posedge clk);
    t1;
    t1;
    expect (@(posedge clk) a |-> ##[1:3] b) try(2, 1); else try(2, 0);
    expect (@(posedge clk) c[*2] ##1 !c) try(3, 1); else try(3, 0);
    expect (@(posedge clk) disable iff (rst) a ##1 b ##1 c) try(4, 1); else try(4, 0);
    for (int i = 0; i < 4; i++)
      expect (@(posedge clk) a ##2 b) try(5, 1); else try(5, 0);
    expect (@(negedge clk) a throughout (b ##1 c)) try(6, 1); else try(6, 0);
    #7;
    expect (@(posedge clk) 1) try(7, 1);
    expect (@(posedge clk) c until_with b) try(8, 1); else try(8, 0);
    check_model;
    if (errors == 0) $display("PASSED");
    $finish;
  end

  // ---- reference model ----
  int m_id[$], m_res[$], m_t[$];
  int pos;      // tick index (posedge k at 5 + 10 k) of the current time step
  bit model_ok = 1;

  function automatic bit smp(bit q[$], int k);   // sampled value, k >= 0
    if (k >= q.size()) begin model_ok = 0; return 0; end
    return q[k];
  endfunction
  // value sampled at negedge m (time 10 m, m >= 1) = value held before the update
  function automatic bit nsmp(bit q[$], int m);
    return smp(q, m - 1);
  endfunction
  function automatic void log(int id, bit r, int tick);   // tick = time step 5 + 10 tick
    m_id.push_back(id); m_res.push_back(r); m_t.push_back(5 + 10 * tick);
    pos = tick;
  endfunction
  // rst sampled in any tick of [s, e]: an aborted evaluation
  function automatic bit rst_in(int s, int e);
    for (int k = s; k <= e; k++) if (smp(prst, k)) return 1;
    return 0;
  endfunction

  task automatic check_model;
    int s, k;
    bit r;
    int rst_hit = 0;
    bit aborted4 = 0;
    pos = 0;
    // 1, 1: a ##1 b
    repeat (2) begin
      s = pos + 1;
      if (!smp(pa, s)) log(1, 0, s);
      else log(1, smp(pb, s + 1), s + 1);
    end
    // 2: a |-> ##[1:3] b ; vacuous pass if !a
    s = pos + 1;
    if (!smp(pa, s)) log(2, 1, s);
    else begin
      r = 0; k = s;
      for (int j = 1; j <= 3; j++) if (!r && smp(pb, s + j)) begin r = 1; k = s + j; end
      if (!r) k = s + 3;
      log(2, r, k);
    end
    // 3: c[*2] ##1 !c
    s = pos + 1;
    if (!smp(pc, s)) log(3, 0, s);
    else if (!smp(pc, s + 1)) log(3, 0, s + 1);
    else log(3, !smp(pc, s + 2), s + 2);
    // 4: disable iff (rst) a ##1 b ##1 c
    s = pos + 1;
    if (smp(prst, s)) begin
      // disabled at the first tick: no attempt result; the statement is assumed to
      // complete at that tick (what an aborted expect does is not specified)
      rst_hit = 1;
      pos = s;
      aborted4 = 1;
    end
    else begin
      int dk;
      if (!smp(pa, s)) begin r = 0; dk = s; end
      else if (!smp(pb, s + 1)) begin r = 0; dk = s + 1; end
      else begin r = smp(pc, s + 2); dk = s + 2; end
      if (rst_in(s + 1, dk)) begin $error("model: rst during expect 4 after its start is not modelled"); errors++; end
      log(4, r, dk);
    end
    // 5 x4: a ##2 b (a at s, b at s+2; fails at s if !a)
    repeat (4) begin
      s = pos + 1;
      if (!smp(pa, s)) log(5, 0, s);
      else log(5, smp(pb, s + 2), s + 2);
    end
    // 6: negedge a throughout (b ##1 c): next negedge strictly after the current time
    begin
      int m = pos + 1;       // negedge m is at 10 (pos + 1), after the posedge pos at 5 + 10 pos
      int dm;
      if (!(nsmp(pa, m) && nsmp(pb, m))) begin r = 0; dm = m; end
      else begin r = nsmp(pa, m + 1) && nsmp(pc, m + 1); dm = m + 1; end
      m_id.push_back(6); m_res.push_back(r); m_t.push_back(10 * dm);
      // #7 after the negedge at 10 dm, then expect 7 passes at the next posedge
      pos = dm;               // posedge dm is at 5 + 10 dm, before 10 dm + 7 ...
      pos = dm + 1;           // ... so the next posedge is tick dm + 1
      m_id.push_back(7); m_res.push_back(1); m_t.push_back(5 + 10 * pos);
    end
    // 8: c until_with b
    s = pos + 1;
    begin
      int kk = s;
      r = 0;
      while (1) begin
        if (kk >= pc.size()) begin model_ok = 0; break; end
        if (!smp(pc, kk)) begin r = 0; break; end
        if (smp(pb, kk)) begin r = 1; break; end
        kk++;
      end
      log(8, r, kk);
    end
    if (!model_ok) begin $error("reference model ran past the recorded samples"); errors++; end
    // compare: walk the model list against the observed list
    begin
      int oi = 0;
      for (int i = 0; i < m_id.size(); i++) begin
        // a result reported for the aborted expect 4 is tolerated
        if (aborted4 && oi < o_id.size() && o_id[oi] == 4 && m_id[i] != 4) oi++;
        if (oi < o_id.size() && o_id[oi] == m_id[i]) begin
          if (o_res[oi] != m_res[i] || o_t[oi] != m_t[i]) begin
            $error("try%0d %s at %0d, expected %s at %0d", m_id[i], o_res[oi] ? "pass" : "fail", o_t[oi],
                   m_res[i] ? "pass" : "fail", m_t[i]);
            errors++;
          end
          oi++;
        end
        else begin
          $error("expect %0d: no result observed (model: %s at %0d)%s", m_id[i], m_res[i] ? "pass" : "fail", m_t[i],
                 m_id[i] == 2 ? " - vacuous success must execute the pass statement" : "");
          errors++;
        end
      end
      if (oi != o_id.size()) begin $error("%0d unexpected extra results", o_id.size() - oi); errors++; end
    end
    if (npass + nfail != o_id.size()) begin $error("counters inconsistent"); errors++; end
  endtask
endmodule
