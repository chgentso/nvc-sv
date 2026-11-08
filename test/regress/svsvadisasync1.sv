// Expected values: IEEE 1800-2017 16.12 (disable iff: the disable condition is asynchronous,
// i.e. checked at any time and not only at clock ticks; an attempt in progress whose
// evaluation interval contains a moment where the condition is true is aborted and
// reports neither success nor failure), 16.14.1 (fail action), 16.14.3 (cover: the pass
// statement runs on a non-vacuous success), 16.5.1 (a sampled in the preponed region:
// a change made at the tick time itself is seen at the next tick).
// A reference model logs a, dis, mode changes and the ticks; for each attempt (a sampled
// 1; b is constantly 0, so every non-aborted attempt fails / the cover sequence matches)
// it predicts the resolution tick and aborts the attempt if the disable condition is true
// anywhere in the open interval (start tick, resolution tick). An attempt whose condition
// rises exactly at the resolution tick, or falls exactly at the start tick, is a same-time
// race and either outcome is accepted.
`timescale 1ns/1ns
module svsvadisasync1;
  bit clk;
  logic dis, a, b;
  logic [1:0] mode;
  int got[string];
  initial forever #5 clk = ~clk;
  function void rec(string s);
    automatic string key = $sformatf("%s %0d", s, $time);
    got[key] = (got.exists(key) ? got[key] : 0) + 1;
  endfunction
  property p; @(posedge clk) disable iff (dis) a |-> ##2 b; endproperty
  property q; @(posedge clk) disable iff (mode == 2'd1 || dis) a |-> ##1 b; endproperty
  ap: assert property (p) else rec("p fail");
  aq: assert property (q) else rec("q fail");
  cp: cover property (disable iff (dis) @(posedge clk) a ##2 !b) rec("cover");
  initial begin
    dis = 0; a = 0; b = 0; mode = 0;
    // glitch between ticks kills attempt started at 15
    #12 a = 1; #1 a = 0; #5 dis = 1; #1 dis = 0;
    // same glitch but attempt completes before the glitch is relevant
    #30 a = 1; #10 a = 0; #1 dis = 1; #2 dis = 0;
    // disable held across ticks
    #30 a = 1; #10 a = 0; #3 dis = 1; #20 dis = 0;
    // glitch after the last tick of an attempt: no effect
    #30 a = 1; #10 a = 0; #12 dis = 1; #1 dis = 0;
    // second condition operand
    #20 a = 1; #10 a = 0; #3 mode = 1; #1 mode = 0;
    #10 a = 1; #10 a = 0;
    #50 check();
    $finish;
  end

  // ---- reference model ----
  int  tk[$];            // tick times
  int  at[$];            // times of changes of a, and the new value
  bit  av[$];
  int  lt[$];            // times of changes of the disable conditions
  bit  lc1[$], lc2[$];   // dis, (mode==1 || dis) after each change
  always @(posedge clk) tk.push_back($time);
  always @(a) begin at.push_back($time); av.push_back(a === 1'b1); end
  // a as sampled in the preponed region at time T: the value after the last change before T
  function automatic bit sampled_a(int T);
    automatic bit v = 0;
    for (int i = 0; i < at.size(); i++) if (at[i] < T) v = av[i];
    return v;
  endfunction
  always @(dis or mode) begin
    lt.push_back($time); lc1.push_back(dis === 1'b1); lc2.push_back((mode === 2'd1) || dis === 1'b1);
  end

  int ex[string], opt[string];
  function automatic void add_ex(string s, int t);
    automatic string key = $sformatf("%s %0d", s, t);
    ex[key] = (ex.exists(key) ? ex[key] : 0) + 1;
  endfunction
  function automatic void add_opt(string s, int t);
    automatic string key = $sformatf("%s %0d", s, t);
    opt[key] = (opt.exists(key) ? opt[key] : 0) + 1;
  endfunction
  // 0: condition never true in (ta,tb), 1: true somewhere inside, 2: same-time race only
  function automatic int disabled(int which, int ta, int tb);
    automatic int race = 0;
    for (int i = 0; i < lt.size(); i++) begin
      automatic bit v = which == 1 ? lc1[i] : lc2[i];
      automatic int s = lt[i];
      automatic int e = (i + 1 < lt.size()) ? lt[i+1] : 1 << 30;
      if (!v || e <= s) continue;
      if (s < tb && e > ta) return 1;
      if (s == tb) race = 2;
    end
    for (int i = 0; i < lt.size(); i++) begin
      automatic bit v = which == 1 ? lc1[i] : lc2[i];
      automatic int e = (i + 1 < lt.size()) ? lt[i+1] : 1 << 30;
      if (v && e == ta && (i + 1 < lt.size())) race = 2;
    end
    return race;
  endfunction
  task automatic check();
    int errs = 0;
    int n = tk.size();
    for (int k = 0; k < n; k++) begin
      if (!sampled_a(tk[k])) continue;
      // p: ##2, disable dis;  q: ##1, disable mode==1||dis;  cover: ##2
      if (k + 2 < n) begin
        automatic int d = disabled(1, tk[k], tk[k+2]);
        if (d == 0) begin add_ex("p fail", tk[k+2]); add_ex("cover", tk[k+2]); end
        else if (d == 2) begin add_opt("p fail", tk[k+2]); add_opt("cover", tk[k+2]); end
      end
      if (k + 1 < n) begin
        automatic int d = disabled(2, tk[k], tk[k+1]);
        if (d == 0) add_ex("q fail", tk[k+1]);
        else if (d == 2) add_opt("q fail", tk[k+1]);
      end
    end
    foreach (ex[s]) if (!got.exists(s) || got[s] != ex[s]) begin
      $error("missing/different event '%s': expected %0d got %0d", s, ex[s], got.exists(s) ? got[s] : 0); errs++;
    end
    foreach (got[s]) if (!ex.exists(s) && !opt.exists(s)) begin $error("unexpected event '%s'", s); errs++; end
    if (ex.num() != 6) begin $error("model sanity: %0d distinct events (expected 6)", ex.num()); errs++; end
    if (errs == 0) $display("PASSED");
  endtask
endmodule
