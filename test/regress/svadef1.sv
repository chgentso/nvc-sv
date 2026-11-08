// Deferred assertions (assert #0, assert final, cover #0) in always blocks
// and at module level: evaluation with blocking values, with values before
// the non-blocking update, flushing of a pending report when the process
// runs again in the same time step (net glitch, module-level assertion
// re-evaluated after a multi-write process), cover, assert final.
// Expected values: IEEE 1800-2017 16.4 (deferred assertions), 16.4.1
// (reports mature in the Reactive region after the process has run; a
// pending report is flushed when the same assertion is re-executed or its
// process is re-triggered in that time step, so only the last evaluation of
// a time step reports), 16.4.2 (module-level deferred assertion is
// like an assertion in always_comb: evaluated at time 0 and when an operand
// changes; assert final reports in the Postponed region), 16.4.3 (the
// arguments of the action subroutine are the values at the evaluation).
// Schedule (clk rises at 5, 15, 25, 35; falls at 10, 20, 30; y = 1 at 12,
// x = 1 at 22, x = 2 at 33; $finish at 38):
//   posedge n=0..3:  a == n and b == n-1 (b = 0 for n = 0) when evaluated
//     assert #0 (a != b)        fails only for n = 0           [id 1: a=0 b=0]
//     assert #0 (a == b)        passes only for n = 0 [id 2: a=0 b=0], fails
//                               for n >= 1                     [id 3]
//   negedge 10, 20, 30: c = 1 when `assert #0 (c == 0)` runs -> fails [id 4: c=1];
//     the second (c == 0) after c = 0 passes (no report)
//   module-level lb: assert #0 (c == 0): last evaluation of each negedge time step
//     sees c = 2 -> fails once per negedge [id 11: c=2]; at time 0 c = 0 passes
//   y = 1 at 12: first evaluation sees y=1, w=0 (flushed) -> only the final
//     pass is reported [id 6: y=1 w=1]; a "glitch fail" is never reported
//   x: at 22 cover #0 (x==1) reports [id 8: x=1] and assert x==2 fails [id 10];
//      at 33 assert x==2 passes [id 9], cover not hit
//   module-level assert final (a != b): evaluated at time 0 with a == b == 0,
//     fails once [id 12: a=0 b=0]; later time steps end with a != b (a is
//     updated after b), glitches are flushed
// A pass of the glitch assertion at time 0 (net w gets its initial value) is
// accepted but not required.
int log_t[$], log_id[$], log_v1[$], log_v2[$];
function void rec(int id, int v1, int v2);
  log_t.push_back($time); log_id.push_back(id); log_v1.push_back(v1); log_v2.push_back(v2);
endfunction
module svadef1;
  logic clk = 0;
  int a = 0, b = 0, c = 0, x = 0, y = 0;
  int errors = 0;
  wire integer w = y;
  always #5 clk = ~clk;
  always @(posedge clk) begin
    a <= a + 1;
    assert #0 (a != b) else rec(1, a, b);
    assert #0 (a == b) rec(2, a, b); else rec(3, 0, 0);
    b = a;
  end
  always @(negedge clk) begin
    c = 1;
    assert #0 (c == 0) else rec(4, c, 0);
    c = 0;
    assert #0 (c == 0) else rec(5, c, 0);
    c = 2;
  end
  always @(y or w) begin
    assert #0 (y == w) rec(6, y, w);
      else rec(7, y, w);
  end
  always @(x) begin
    cover #0 (x == 1) rec(8, x, 0);
    assert #0 (x == 2) rec(9, x, 0); else rec(10, x, 0);
  end
  lb: assert #0 (c == 0) else rec(11, c, 0);
  assert final (a != b) else rec(12, a, b);
  initial begin
    #12 y = 1;
    #10 x = 1;
    #11 x = 2;
    #5;
    // expected reports: time, id, v1, v2 (v1/v2 -1: not checked)
    begin
      automatic int et[$] = '{0, 5, 5, 10, 12, 15, 20, 22, 22, 25, 30, 33, 35, 10, 20, 30};
      automatic int ei[$] = '{12, 1, 2, 4, 6, 3, 4, 8, 10, 3, 4, 9, 3, 11, 11, 11};
      automatic int e1[$] = '{0, 0, 0, 1, 1, -1, 1, 1, -1, -1, 1, -1, -1, 2, 2, 2};
      automatic int e2[$] = '{0, 0, 0, -1, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
      bit used[$];
      foreach (log_id[i]) used.push_back(0);
      foreach (et[k]) begin
        automatic int found = -1;
        foreach (log_id[i]) if (!used[i] && log_t[i] == et[k] && log_id[i] == ei[k]) begin found = i; break; end
        if (found < 0) begin
          $error("missing report: time %0d id %0d", et[k], ei[k]); errors++;
        end else begin
          used[found] = 1;
          if ((e1[k] != -1 && log_v1[found] != e1[k]) || (e2[k] != -1 && log_v2[found] != e2[k])) begin
            $error("report time %0d id %0d: values %0d %0d expected %0d %0d", et[k], ei[k],
                   log_v1[found], log_v2[found], e1[k], e2[k]); errors++;
          end
        end
      end
      foreach (log_id[i])
        if (!used[i] && !(log_t[i] == 0 && log_id[i] == 6)) begin
          $error("unexpected report: time %0d id %0d (%0d %0d)", log_t[i], log_id[i], log_v1[i], log_v2[i]);
          errors++;
        end
    end
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
