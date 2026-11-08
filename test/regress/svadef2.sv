// Deferred assertions (assert #0 and assert final) inside a class task:
// the condition is evaluated when the statement is reached with the values
// at that moment, the action block runs later in the same time step with the
// values captured at evaluation (local variable and module variable have
// changed by then).
// Expected values: IEEE 1800-2017 16.4 (deferred assertions), 16.4.2
// (observed deferred: report in the Reactive region of the time step, after
// the process has run on), 16.4.3 (final deferred: report in the Postponed
// region, so after all reports of the Reactive region; actual arguments of
// the action subroutine are evaluated when the assertion is evaluated).
// Reports are recorded with their time and the captured values; the
// expected reports for run(3) at time 0 and run(4) at time 10 are:
//   a pass (g == k ok):      k, loc = 2k, g = k
//   b fail (g = k+1):        k, loc = 99, g = k+1
//   c fail (assert final):   k, g = k+1
// and "run done" (immediate, Active region) must precede a and b, which
// precede c.  The order of a and b with respect to each other is not
// specified.
int g = 0;
int log_t[$], log_id[$], log_k[$], log_loc[$], log_g[$];
// ids: 1 a pass, 2 a fail, 3 b pass, 4 b fail, 5 c pass, 6 c fail, 7 run done
function void rec(int id, int k, int loc, int gg);
  log_t.push_back($time); log_id.push_back(id); log_k.push_back(k);
  log_loc.push_back(loc); log_g.push_back(gg);
endfunction
class env;
  int m = 7;
  task run(int k);
    int loc = k * 2;
    g = k;
    assert #0 (g == k) rec(1, k, loc, g);
      else rec(2, k, loc, g);
    g = k + 1;
    loc = 99;
    assert #0 (g == k) rec(3, k, loc, g);
      else rec(4, k, loc, g);
    assert final (g == k) rec(5, k, 0, g);
      else rec(6, k, 0, g);
    g = k;
    rec(7, k, loc, g);
  endtask
endclass
module svadef2;
  env e = new;
  int errors = 0;
  function automatic int find(int t, int id);
    int n = 0, pos = -1;
    foreach (log_id[i]) if (log_t[i] == t && log_id[i] == id) begin n++; pos = i; end
    if (n != 1) begin $error("time %0d: report %0d seen %0d times (expected once)", t, id, n); errors++; return -1; end
    return pos;
  endfunction
  task automatic check(int t, int k);
    int pa = find(t, 1), pb = find(t, 4), pc = find(t, 6), pd = find(t, 7);
    if (pa < 0 || pb < 0 || pc < 0 || pd < 0) return;
    if (log_k[pa] != k || log_loc[pa] != 2 * k || log_g[pa] != k) begin
      $error("t=%0d a: k=%0d loc=%0d g=%0d, expected k=%0d loc=%0d g=%0d", t, log_k[pa], log_loc[pa], log_g[pa], k, 2 * k, k); errors++; end
    if (log_k[pb] != k || log_loc[pb] != 99 || log_g[pb] != k + 1) begin
      $error("t=%0d b: k=%0d loc=%0d g=%0d, expected k=%0d loc=99 g=%0d", t, log_k[pb], log_loc[pb], log_g[pb], k, k + 1); errors++; end
    if (log_k[pc] != k || log_g[pc] != k + 1) begin
      $error("t=%0d c: k=%0d g=%0d, expected k=%0d g=%0d", t, log_k[pc], log_g[pc], k, k + 1); errors++; end
    if (!(pd < pa && pd < pb && pa < pc && pb < pc)) begin
      $error("t=%0d: report order wrong (done %0d a %0d b %0d c %0d)", t, pd, pa, pb, pc); errors++; end
  endtask
  initial begin
    e.run(3);
    #10 e.run(4);
    #1;
    check(0, 3);
    check(10, 4);
    if (log_id.size() != 8) begin $error("%0d reports, expected 8", log_id.size()); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
