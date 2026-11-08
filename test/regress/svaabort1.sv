`timescale 1ns/1ns
// Property abort operators accept_on, reject_on, sync_accept_on and
// sync_reject_on on implications, with a fixed stimulus.
// Expected values: IEEE 1800-2017 16.12.14 (abort properties: when the abort
// condition is true the property evaluates to true (accept_on) or false
// (reject_on) irrespective of the evaluation of the operand at that time; the
// sync_ forms are checked only at clock ticks), 16.12.7 (|-> and |=>),
// 16.14.1 (a vacuous success is a success: the pass action runs), 16.9.3
// (values sampled at the tick).
//
// Reference model: the values of a, b, r, s sampled at the posedges 5, 15, ..., 135
// (tick k = time 5 + 10 k, k = 0..13) are recorded.  r and s change at the
// negedges, so evaluating an abort condition asynchronously (accept_on, reject_on)
// or at the next tick sees the same value in the same step index time / 10.
// Per attempt that starts at tick t and per property, the model gives the tick
// at which the pass or the fail action runs:
//   p1 accept_on (r) a |=> b       tick t: r -> pass; !a -> pass (vacuous);
//                                  tick t+1: r -> pass; else b -> pass / fail
//   p2 reject_on (r) a |-> ##1 b   tick t: r -> fail; !a -> pass (vacuous);
//                                  tick t+1: r -> fail; else b -> pass / fail
//   p4 sync_accept_on (s) a |=> ##1 b  ticks t, t+1, t+2: s -> pass; !a at t -> pass;
//                                  else b at t+2 -> pass / fail
//   p5 sync_reject_on (s) (a |-> ##2 b) ticks t, t+1, t+2: s -> fail; !a at t -> pass;
//                                  else b at t+2 -> pass / fail
// The action sets bit time/10 of pm[i] (pass) or fm[i] (fail).  The vectors are
// compared at 139 ns, after the actions of the last tick (135) have run.
// Attempts still running after the last tick have no action.
module svaabort1;
  // VAC = 1: the pass action of a vacuous success runs (16.14.1); 0 is only used
  // to look at the remaining checks separately from that rule.
  parameter bit VAC = 1;
  logic clk = 0, a = 0, b = 0, r = 0, s = 0;
  int pm[1:6], fm[1:6];
  int errors = 0;
  always #5 clk = ~clk;
  initial begin
    for (int i = 1; i <= 5; i++) begin pm[i] = 0; fm[i] = 0; end
    // drive on negedge so values are stable at posedges
    #140 $finish;
  end
  // fixed stimulus instead of random
  logic [0:13] A = 14'b01101001100101, B = 14'b00100110010011, R = 14'b00000100000010, S = 14'b00000000100000;
  int n = 0;
  always @(negedge clk) begin a <= A[n]; b <= B[n]; r <= R[n]; s <= S[n]; n <= n + 1; end

  localparam int NT = 14;
  int sa[NT], sb[NT], sr[NT], ss[NT];
  int ntick = 0;
  always @(posedge clk) begin
    if (ntick < NT) begin sa[ntick] = a; sb[ntick] = b; sr[ntick] = r; ss[ntick] = s; end
    ntick++;
  end

  p1: assert property (@(posedge clk) accept_on (r) a |=> b) pm[1] |= 1 << ($time/10); else fm[1] |= 1 << ($time/10);
  p2: assert property (@(posedge clk) reject_on (r) a |-> ##1 b) pm[2] |= 1 << ($time/10); else fm[2] |= 1 << ($time/10);
  p4: assert property (@(posedge clk) sync_accept_on (s) a |=> ##1 b) pm[4] |= 1 << ($time/10); else fm[4] |= 1 << ($time/10);
  p5: assert property (@(posedge clk) sync_reject_on (s) (a |-> ##2 b)) pm[5] |= 1 << ($time/10); else fm[5] |= 1 << ($time/10);

  // result of the attempt of property p started at tick t: returns 1 = pass,
  // 3 = vacuous pass, 0 = fail, 2 = no result yet, and the tick of the action in rt
  function automatic int attempt(int p, int t, output int rt);
    int k;
    rt = -1;
    case (p)
      1: begin
        if (sr[t]) begin rt = t; return 1; end
        if (!sa[t]) begin rt = t; return 3; end
        if (t + 1 >= NT) return 2;
        rt = t + 1;
        if (sr[t + 1]) return 1;
        return sb[t + 1];
      end
      2: begin
        if (sr[t]) begin rt = t; return 0; end
        if (!sa[t]) begin rt = t; return 3; end
        if (t + 1 >= NT) return 2;
        rt = t + 1;
        if (sr[t + 1]) return 0;
        return sb[t + 1];
      end
      4: begin
        if (ss[t]) begin rt = t; return 1; end
        if (!sa[t]) begin rt = t; return 3; end
        if (t + 1 >= NT) return 2;
        if (ss[t + 1]) begin rt = t + 1; return 1; end
        if (t + 2 >= NT) return 2;
        rt = t + 2;
        if (ss[t + 2]) return 1;
        return sb[t + 2];
      end
      5: begin
        if (ss[t]) begin rt = t; return 0; end
        if (!sa[t]) begin rt = t; return 3; end
        if (t + 1 >= NT) return 2;
        if (ss[t + 1]) begin rt = t + 1; return 0; end
        if (t + 2 >= NT) return 2;
        rt = t + 2;
        if (ss[t + 2]) return 0;
        return sb[t + 2];
      end
    endcase
    return 2;
  endfunction

  function automatic string bits(int v);
    string str = "";
    for (int i = NT - 1; i >= 0; i--) str = {str, v[i] ? "1" : "0"};
    return str;
  endfunction

  initial begin
    #139;
    if (ntick != NT) begin $error("%0d ticks sampled, expected %0d", ntick, NT); errors++; end
    foreach (pm[p]) begin
      automatic int ep = 0, ef = 0;
      if (p == 3 || p == 6) begin
        if (pm[p] !== 0 || fm[p] !== 0) begin $error("p%0d was never assigned but changed", p); errors++; end
        continue;
      end
      for (int t = 0; t < NT; t++) begin
        int res, rt;
        res = attempt(p, t, rt);
        if (res == 1 || (res == 3 && VAC)) ep |= 1 << rt;
        else if (res == 0) ef |= 1 << rt;
      end
      if (pm[p] !== ep || fm[p] !== ef) begin
        $error("p%0d pass=%s fail=%s, expected pass=%s fail=%s", p, bits(pm[p]), bits(fm[p]), bits(ep), bits(ef)); errors++;
      end
    end
    if (errors == 0) $display("PASSED");
  end
endmodule
