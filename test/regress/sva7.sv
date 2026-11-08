`timescale 1ns/1ns
// Immediate assertions: simple procedural assert / assume / cover with pass and
// fail actions, labelled assertions, and module-level deferred assertions
// (assert #0, assert final).
// Expected values: IEEE 1800-2017 16.3 (immediate assert, assume, cover: pass
// statement when the expression is true, else statement when false; no action
// when the statement for that outcome is omitted; cover runs its statement only
// when true), 16.4 (deferred immediate assertions: outside a procedure the
// assertion behaves as if wrapped in an always_comb, so it is evaluated once at
// time zero and again whenever a value it reads changes; the report of #0
// is made in the Reactive region and of final in the Postponed region of the
// same time step), 9.2.2.2 (always_comb), 4.7 / 9.4.2 (x <= x + 1).
// x changes at the posedges 5 (to 1) and 15 (to 2) only ($finish at 21).  The
// deferred assertions therefore report at 0 (x = 0), 5 (x = 1) and 15 (x = 2),
// all passing (x != 3 and x < 100).  The order between the two reports of one time
// step is not checked.
module sva7;
  logic clk = 0;
  int x = 0, y = 5;
  int errors = 0;
  int a0_n, a0_t[4], a0_x[4], a0_fail;
  int pass[10], fail[10], cov[10];
  always #5 clk = ~clk;
  always @(posedge clk) x <= x + 1;
  a0: assert #0 (x != 3) begin
    if (a0_n < 4) begin a0_t[a0_n] = $time; a0_x[a0_n] = x; end
    a0_n++;
  end else a0_fail++;
  assert final (x < 100) $display("A1 pass t=%0d x=%0d", $time, x);
  initial begin
    #1;
    assert (x == 0) pass[0]++; else fail[0]++;
    assert (x == 7) pass[1]++; else fail[1]++;
    assert (x == 7) else fail[2]++;
    assert (x == 0) else fail[3]++;
    assume (x == 0) pass[4]++; else fail[4]++;
    cover (x == 0) cov[5]++;
    cover (x == 9) cov[6]++;
    lbl: assert (y == 5) pass[7]++;
    #20;
    assert (x == 2) pass[8]++; else fail[8]++;
    // procedural assertions: pass counts and fail counts
    for (int i = 0; i < 10; i++) begin
      automatic int ep = 0, ef = 0;
      case (i)
        0: ep = 1;            // x == 0 true
        1: ef = 1;            // x == 7 false
        2: ef = 1;            // x == 7 false, only an else action
        3: ef = 0;            // x == 0 true, only an else action: nothing runs
        4: ep = 1;            // assume true
        7: ep = 1;            // y == 5 true
        8: ep = 1;            // x == 2 at 21
        default: ;
      endcase
      if (pass[i] !== ep || fail[i] !== ef) begin
        $error("immediate assertion %0d: %0d pass and %0d fail actions, expected %0d and %0d", i, pass[i], fail[i], ep, ef); errors++;
      end
    end
    if (cov[5] !== 1 || cov[6] !== 0) begin $error("cover actions: %0d and %0d, expected 1 and 0", cov[5], cov[6]); errors++; end
    // deferred assertions
    if (a0_fail !== 0) begin $error("a0 failed %0d times", a0_fail); errors++; end
    if (a0_n !== 3 || a0_t[0] !== 0 || a0_x[0] !== 0 || a0_t[1] !== 5 || a0_x[1] !== 1 || a0_t[2] !== 15 || a0_x[2] !== 2) begin
      $error("a0 reports: %0d (t=%0d x=%0d, t=%0d x=%0d, t=%0d x=%0d), expected 3 (0/0, 5/1, 15/2)",
             a0_n, a0_t[0], a0_x[0], a0_t[1], a0_x[1], a0_t[2], a0_x[2]); errors++;
    end
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
