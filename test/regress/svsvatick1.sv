// Expected values: IEEE 1800-2017 16.14.1 (fail action), 16.5.1 (values sampled in the
// preponed region of the clock-tick time step: a change made at the tick time is seen at
// the next tick), 16.9.2 (##n), 16.12.6 (|->), 16.16 (clocking events: posedge/negedge of
// the clock, and of a same-named clock in a generate block, which is a different signal),
// 27.5 (generate). A reference model logs the changes of a and b and every tick of the three
// clocks; for each attempt (a or b sampled 1 at a tick) it predicts the failures.
// Only fail actions exist, so vacuous-success behaviour is not involved.
`timescale 1ns/1ns
module svsvatick1;
  bit clk;
  logic a, b;
  int got[string];
  initial forever #5 clk = ~clk;
  function void rec(string s);
    automatic string key = $sformatf("%0d %s", $time, s);
    got[key] = (got.exists(key) ? got[key] : 0) + 1;
  endfunction

  // Same clock in three assertions and a clock of the same name in a
  // generate block which is a different signal
  p1: assert property (@(posedge clk) a |-> ##1 b) else rec("p1 fail");
  p2: assert property (@(posedge clk) a |-> ##2 b) else rec("p2 fail");
  p3: assert property (@(negedge clk) a |-> b) else rec("p3 fail");
  p4: assert property (@(posedge clk) b |-> ##1 !b) else rec("p4 fail");

  generate if (1) begin : g
    bit clk;
    initial forever #8 clk = ~clk;
    q1: assert property (@(posedge clk) a |-> ##1 b) else rec("g.q1 fail");
    q2: assert property (@(posedge clk) a |-> b) else rec("g.q2 fail");
  end endgenerate

  initial begin
    a = 0; b = 0;
    #12 a = 1; #10 a = 0;
    #20 b = 1; #10 b = 0;
    #20 a = 1; b = 1; #10 a = 0; b = 0;
    #50 check();
    $finish;
  end

  // ---- reference model ----
  int tp[$], tn[$], tg[$];       // tick times of posedge clk, negedge clk, posedge g.clk
  int ta[$], tb[$];              // times of changes of a and b
  bit va[$], vb[$];              // new values
  always @(posedge clk) tp.push_back($time);
  always @(negedge clk) tn.push_back($time);
  always @(posedge g.clk) tg.push_back($time);
  always @(a) begin ta.push_back($time); va.push_back(a === 1'b1); end
  always @(b) begin tb.push_back($time); vb.push_back(b === 1'b1); end
  function automatic bit sa(int T);
    automatic bit v = 0;
    for (int i = 0; i < ta.size(); i++) if (ta[i] < T) v = va[i];
    return v;
  endfunction
  function automatic bit sb(int T);
    automatic bit v = 0;
    for (int i = 0; i < tb.size(); i++) if (tb[i] < T) v = vb[i];
    return v;
  endfunction
  int ex[string];
  function automatic void add(string s, int t);
    automatic string key = $sformatf("%0d %s", t, s);
    ex[key] = (ex.exists(key) ? ex[key] : 0) + 1;
  endfunction
  task automatic check();
    int errs = 0;
    for (int k = 0; k < tp.size(); k++) begin
      if (sa(tp[k]) && k + 1 < tp.size() && !sb(tp[k+1])) add("p1 fail", tp[k+1]);
      if (sa(tp[k]) && k + 2 < tp.size() && !sb(tp[k+2])) add("p2 fail", tp[k+2]);
      if (sb(tp[k]) && k + 1 < tp.size() && sb(tp[k+1])) add("p4 fail", tp[k+1]);
    end
    for (int k = 0; k < tn.size(); k++) if (sa(tn[k]) && !sb(tn[k])) add("p3 fail", tn[k]);
    for (int k = 0; k < tg.size(); k++) begin
      if (sa(tg[k]) && k + 1 < tg.size() && !sb(tg[k+1])) add("g.q1 fail", tg[k+1]);
      if (sa(tg[k]) && !sb(tg[k])) add("g.q2 fail", tg[k]);
    end
    foreach (ex[s]) if (!got.exists(s) || got[s] != ex[s]) begin
      $error("missing/different event '%s': expected %0d got %0d", s, ex[s], got.exists(s) ? got[s] : 0); errs++;
    end
    foreach (got[s]) if (!ex.exists(s)) begin $error("unexpected event '%s'", s); errs++; end
    if (ex.num() == 0) begin $error("model sanity: no events"); errs++; end
    if (tp.size() != 13 || tn.size() != 13 || tg.size() != 8) begin $error("tick counts %0d %0d %0d", tp.size(), tn.size(), tg.size()); errs++; end
    if (errs == 0) $display("PASSED");
  endtask
endmodule
