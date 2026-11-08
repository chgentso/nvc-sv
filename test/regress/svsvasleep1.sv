// Expected values: IEEE 1800-2017 16.9.3 (sampled values, $rose/$fell/$stable/
// $changed/$past/$isunknown), 16.12.6 (|-> and |=>), 16.12.? (##N, ##[m:n]),
// 16.12.14 (disable iff), 16.14 (assert/cover action blocks), 16.15,
// 6.8 (an
// uninitialised logic is X; ~X = X so a clock stuck at X never ticks).
// Reference model: sampler processes record the preponed values just before
// every clock event (posedge clk at 5+10e, negedge clk at 10+10k,
// posedge clk2 at 7+14j); each property is then evaluated procedurally on the
// recorded samples and the exact failure times (or counts) are compared with
// the assertion action blocks.  An attempt is discarded when rst (current
// value, asynchronous) is high at any time while it is pending: rst is high
// before 23 and during (626,629).
// Not relied on: whether vacuous successes run the pass action (a2 pass count
// is checked as a range) and $stable/$changed on X values (a9 results in the
// window where d is X are ignored).
interface bus_if(input logic clk);
  logic req, ack;
  int ack_fail_t[$];
  property p_ack; @(posedge clk) req |-> ##[1:2] ack; endproperty
  a_ack: assert property (p_ack) else ack_fail_t.push_back($time);
endinterface

module svsvasleep1;
  bit clk, clk2, rst;
  logic [3:0] cnt;
  logic a, b, c;
  logic [7:0] d;
  int fails, passes, covers, fails7;
  int g8_fires;
  longint f1[$], f2[$], f3[$], f4[$], f5[$], f6[$], f9[$], f10[$], f11[$];
  initial forever #5 clk = ~clk;
  initial forever #7 clk2 = ~clk2;
  bus_if bi(clk);
  wire w_req = bi.req, w_ack = bi.ack;

  // NBA changes in the time step of the clock edge
  always @(posedge clk) begin
    if (rst) cnt <= 0; else cnt <= cnt + 1;
    a <= cnt == 3 || cnt == 9;
    b <= cnt == 5;
  end

  a1: assert property (@(posedge clk) disable iff (rst) a |-> ##2 b)
    else f1.push_back($time);
  a2: assert property (@(posedge clk) disable iff (rst) a |-> ##[1:3] b)
    passes++; else begin fails++; f2.push_back($time); end
  a3: assert property (@(posedge clk) disable iff (rst) (cnt == 4'd7) |=> (cnt == 4'd8))
    else f3.push_back($time);
  a4: assert property (@(posedge clk) disable iff (rst) $rose(a) |-> ##1 !a)
    else f4.push_back($time);
  a5: assert property (@(negedge clk) c |-> d[0])
    else f5.push_back($time);
  a6: assert property (@(posedge clk2) c |-> ##1 !c)
    else f6.push_back($time);
  a7: assert property (@(posedge clk) !$isunknown(d))
    else fails7++;
  a9: assert property (@(posedge clk) disable iff (rst) $changed(d) |-> ##1 $stable(d))
    else f9.push_back($time);
  a10: assert property (@(posedge clk) disable iff (rst) c |-> $past(d, 2) == 8'h01)
    else f10.push_back($time);
  a11: assert property (@(posedge clk) $fell(c) |-> $past(c, 3))
    else f11.push_back($time);
  c1: cover property (@(posedge clk) disable iff (rst) a ##2 b) covers++;
  c2: cover property (@(posedge clk) c) covers++;

  generate if (1) begin : g
    logic clk;
    initial forever #11 clk = ~clk;
    a8: assert property (@(posedge clk) a |-> b)
      else g8_fires++;
  end endgenerate

  initial begin
    rst = 1; c = 0; d = 8'h01; bi.req = 0; bi.ack = 0;
    #23 rst = 0;
    // idle for a long time, then glitches between ticks
    #200 c = 1; #1 c = 0;
    #50 d = 8'h00; #3 c = 1; #4 c = 0;
    #20 bi.req = 1; #10 bi.req = 0;
    #5 bi.ack = 1; #10 bi.ack = 0;
    #30 c = 1; #20 c = 0;
    #10 d = 8'hx; #20 d = 8'h01;
    #100 bi.req = 1; #6 bi.req = 0; #2 bi.ack = 1; #1 bi.ack = 0;
    #30 d = 8'h02; #10 d = 8'h03; #40 d = 8'h01; c = 1; #11 c = 0;
    #20 rst = 1; #3 rst = 0;
    #200;
    check();
    $finish;
  end

  // ---------------- reference model ----------------
  localparam int NE = 83, NN = 82, NC = 59;   // posedge clk, negedge clk, posedge clk2 samples
  logic Sa[NE], Sb[NE], Sc[NE], Srq[NE], Sak[NE];
  logic [7:0] Sd[NE];
  logic [3:0] Scnt[NE];
  logic Nc[NN]; logic [7:0] Nd[NN];
  logic C2[NC];
  // Samplers: the preponed value at an edge at time T is the settled value at the
  // end of time T-1 (all stimulus events are at integer times); '#0' lets every
  // active event of that time step finish first.
  initial begin #4;  for (int e = 0; e < NE; e++) begin #0;
    Sa[e] = a; Sb[e] = b; Sc[e] = c; Sd[e] = d; Scnt[e] = cnt; Srq[e] = bi.req; Sak[e] = bi.ack; #10; end end
  initial begin #9;  for (int k = 0; k < NN; k++) begin #0; Nc[k] = c; Nd[k] = d; #10; end end
  initial begin #6;  for (int j = 0; j < NC; j++) begin #0; C2[j] = c; #14; end end
  int ie = NE, in_ = NN, ic = NC;

  function automatic bit T(logic x); return x === 1'b1; endfunction
  function automatic longint te(int e); return 5 + 10 * e; endfunction
  // attempt starting at edge s and ending at edge en is aborted by rst
  function automatic bit dis(int s, int en); return (s <= 1) || (s <= 62 && en >= 63); endfunction

  int errs;
  task automatic cmp(string n, longint got[$], longint exp[$], longint lo = 0, longint hi = 1000000);
    longint g[$], x[$];
    foreach (got[i]) if (got[i] < lo || got[i] > hi) g.push_back(got[i]);
    foreach (exp[i]) if (exp[i] < lo || exp[i] > hi) x.push_back(exp[i]);
    g.sort(); x.sort();
    if (g.size() != x.size()) begin errs++; $error("%s: %0d failures %p, expected %0d %p", n, g.size(), g, x.size(), x); end
    else foreach (x[i]) if (g[i] != x[i]) begin errs++; $error("%s: failure times %p, expected %p", n, g, x); break; end
  endtask

  task automatic check();
    longint e1[$], e2[$], e3[$], e4[$], e5[$], e6[$], e9[$], e10[$], e11[$], eack[$], gack[$];
    int nonvac, vac, c1, c2;
    if (ie != NE || in_ != NN || ic != NC) begin errs++; $error("sample counts %0d %0d %0d, expected %0d %0d %0d", ie, in_, ic, NE, NN, NC); end
    for (int e = 0; e < NE; e++) begin
      if (e + 2 < NE && !dis(e, e + 2) && T(Sa[e]) && !T(Sb[e + 2])) e1.push_back(te(e + 2));
      if (e + 3 < NE && !dis(e, e + 3) && T(Sa[e])) begin
        if (T(Sb[e+1]) || T(Sb[e+2]) || T(Sb[e+3])) nonvac++; else e2.push_back(te(e + 3));
      end
      if (!dis(e, e) && !T(Sa[e])) vac++;
      if (e + 1 < NE && !dis(e, e + 1) && T(Scnt[e] == 4'd7) && !T(Scnt[e+1] == 4'd8)) e3.push_back(te(e + 1));
      if (e >= 1 && e + 1 < NE && !dis(e, e + 1) && Sa[e] === 1'b1 && Sa[e-1] === 1'b0 && !(Sa[e+1] === 1'b0)) e4.push_back(te(e + 1));
      if (e + 1 < NE && !dis(e, e + 1) && e >= 1 && Sd[e] !== Sd[e-1] && !(Sd[e+1] === Sd[e])) e9.push_back(te(e + 1));
      if (e >= 2 && !dis(e, e) && T(Sc[e]) && !T(Sd[e-2] == 8'h01)) e10.push_back(te(e));
      if (e >= 3 && Sc[e] === 1'b0 && Sc[e-1] === 1'b1 && !T(Sc[e-3])) e11.push_back(te(e));
      if (e + 2 < NE && !dis(e, e + 2) && T(Sa[e]) && T(Sb[e + 2])) c1++;
      if (T(Sc[e])) c2++;
      if (e + 2 < NE && T(Srq[e]) && !T(Sak[e+1]) && !T(Sak[e+2])) eack.push_back(te(e + 2));
    end
    for (int k = 0; k < NN; k++) if (T(Nc[k]) && !T(Nd[k][0])) e5.push_back(10 + 10 * k);
    for (int j = 0; j + 1 < NC; j++) if (T(C2[j]) && !(C2[j+1] === 1'b0)) e6.push_back(7 + 14 * (j + 1));
    cmp("a1", f1, e1); cmp("a2", f2, e2); cmp("a3", f3, e3); cmp("a4", f4, e4);
    cmp("a5", f5, e5); cmp("a6", f6, e6);
    cmp("a9", f9, e9, 0, 394);  cmp("a9", f9, e9, 426, 1000000);
    cmp("a10", f10, e10); cmp("a11", f11, e11);
    for (int i = 0; i < bi.ack_fail_t.size(); i++) gack.push_back(bi.ack_fail_t[i]);
    cmp("bus_if.a_ack", gack, eack);
    // a7: one failure per sampled X on d
    begin
      int ex7;
      for (int e = 0; e < NE; e++) if ($isunknown(Sd[e])) ex7++;
      if (fails7 != ex7) begin errs++; $error("a7: %0d failures, expected %0d", fails7, ex7); end
    end
    if (fails != e2.size()) begin errs++; $error("a2 fails counter %0d vs %0d", fails, e2.size()); end
    if (passes < nonvac || passes > nonvac + vac) begin errs++; $error("a2 passes %0d outside %0d..%0d", passes, nonvac, nonvac + vac); end
    if (covers != c1 + c2) begin errs++; $error("covers %0d, expected %0d (c1 %0d, c2 %0d)", covers, c1 + c2, c1, c2); end
    if (g8_fires != 0) begin errs++; $error("g.a8 evaluated %0d times though g.clk never has an edge", g8_fires); end
    // sanity of the model itself: the stimulus must exercise the interesting cases
    if (e1.size() == 0 || e2.size() == 0 || e5.size() == 0 || e6.size() == 0 || e10.size() == 0 || e11.size() == 0 || e9.size() == 0) begin
      errs++; $error("model sanity: some property never failed in the model");
    end
    if (errs == 0) $display("PASSED");
  endtask
endmodule
