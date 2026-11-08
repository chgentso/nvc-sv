// Expected values: IEEE 1800-2017 16.9.2 (repetition [*n], ##n, ##[m:n]), 16.12.6
// (implication |->: vacuous success when the antecedent does not match),
// 16.14.1 (pass action on success including vacuous success, fail action on failure),
// 16.5.1/16.9.3 ($rose and sampled values: preponed region), 16.14.5 (clock ticks).
// A reference model evaluates every attempt on the values sampled at each posedge and
// predicts the time and result of each pass/fail action; the observed set of
// (assertion, result, time) events must equal the predicted set.
`timescale 1ns/1ns
module chk #(parameter int unsigned Skew = 1)(input logic clk, input logic a, input logic b);
  localparam int L = Skew * 2;
  int cnt[string];
  function void rec(string s);
    automatic string key = $sformatf("%s %0d", s, $time);
    cnt[key] = (cnt.exists(key) ? cnt[key] : 0) + 1;
  endfunction
  A1: assert property (@(posedge clk) $rose(a) |-> b [* (Skew + 1)] ##1 !b)
    rec("A1 pass"); else rec("A1 fail");
  A2: assert property (@(posedge clk) $rose(a) |-> ##(Skew) b)
    rec("A2 pass"); else rec("A2 fail");
  A3: assert property (@(posedge clk) $rose(a) |-> ##[1:Skew + 1] b)
    rec("A3 pass"); else rec("A3 fail");
  A4: assert property (@(posedge clk) $rose(a) |-> ##1 a [* L])
    rec("A4 pass"); else rec("A4 fail");
endmodule
module svsvabound1;
  localparam int Skew = 2;
  localparam int L = Skew * 2;
  logic clk = 0, a = 0, b = 0;
  chk #(.Skew(Skew)) c(.*);
  always #5 clk = ~clk;
  // sampled history, one entry per posedge (stimulus changes at negedges only)
  bit sa[$], sb[$];
  always @(posedge clk) begin sa.push_back(a); sb.push_back(b); end
  initial begin
    @(negedge clk); a = 1; b = 1;
    repeat (3) @(negedge clk);
    b = 0;
    repeat (3) @(negedge clk);
    a = 0;
    repeat (2) @(negedge clk);
    a = 1; b = 0;
    repeat (6) @(negedge clk);
    check();
    $finish;
  end

  int ex[string];
  function automatic void add(string s, int k);
    automatic string key = $sformatf("%s %0d", s, 5 + 10 * k);
    ex[key] = (ex.exists(key) ? ex[key] : 0) + 1;
  endfunction
  task automatic check();
    int n = sa.size();
    int errs = 0;
    int got[string];
    for (int k = 0; k < n; k++) begin
      bit rose = (k > 0) ? (sa[k] && !sa[k-1]) : 1'b0;
      int res;
      if (!rose) begin
        add("A1 pass", k); add("A2 pass", k); add("A3 pass", k); add("A4 pass", k);
      end else begin
        // A1: b[*Skew+1] ##1 !b
        res = -1;
        for (int j = 0; j <= Skew + 1 && res < 0; j++) begin
          if (k + j >= n) break;
          if (j <= Skew) begin
            if (!sb[k+j]) begin add("A1 fail", k + j); res = 1; end
          end else begin
            if (sb[k+j]) add("A1 fail", k + j); else add("A1 pass", k + j);
            res = 1;
          end
        end
        // A2: ##Skew b
        if (k + Skew < n) add(sb[k+Skew] ? "A2 pass" : "A2 fail", k + Skew);
        // A3: ##[1:Skew+1] b : pass at the first b, fail at the end if none
        res = -1;
        for (int j = 1; j <= Skew + 1 && res < 0; j++) begin
          if (k + j >= n) break;
          if (sb[k+j]) begin add("A3 pass", k + j); res = 1; end
          else if (j == Skew + 1) begin add("A3 fail", k + j); res = 1; end
        end
        // A4: ##1 a[*L]
        res = -1;
        for (int j = 1; j <= L && res < 0; j++) begin
          if (k + j >= n) break;
          if (!sa[k+j]) begin add("A4 fail", k + j); res = 1; end
          else if (j == L) begin add("A4 pass", k + j); res = 1; end
        end
      end
    end
    foreach (ex[s]) if (!c.cnt.exists(s) || c.cnt[s] != ex[s]) begin
      $error("missing/different event '%s': expected %0d got %0d", s, ex[s], c.cnt.exists(s) ? c.cnt[s] : 0); errs++;
    end
    got = c.cnt;
    foreach (got[s]) if (!ex.exists(s)) begin $error("unexpected event '%s'", s); errs++; end
    if (c.cnt.num() == 0) begin $error("no events"); errs++; end
    if (errs == 0) $display("PASSED");
  endtask
endmodule
