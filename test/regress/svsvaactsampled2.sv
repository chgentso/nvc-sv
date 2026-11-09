// Sampled value functions in the action blocks of concurrent assertions
// (IEEE 1800-2017 16.9.3, 16.14.6): $rose, $fell, $stable, $changed with an
// explicit clocking event, $past with a delay and gating, a function task
// call receiving $past, a cover property, and a checker.
// Ticks k = 0.. at 5 + 10 k sample c == k; s is 1 only at the sample k == 3.
checker chk(logic clk, int v);
  int pchk = 0;
  assert property (@(posedge clk) 1) if ($sampled(v) == 7) pchk = $past(v, 3);
endchecker
module svsvaactsampled2;
  logic clk = 0;
  int c = 0;
  int errors = 0;
  int r1 = 0, f1 = 0, st = 0, ch = 0, pg = -1, pt = -1, pcov = -1;
  always #5 clk = ~clk;
  always @(posedge clk) c <= c + 1;
  logic s = 0;
  always @(posedge clk) s <= (c == 2);
  task automatic note(int v); pt = v; endtask
  assert property (@(posedge clk) 1)
    begin
      if ($rose(s, @(posedge clk))) r1++;
      if ($fell(s, @(posedge clk))) f1++;
      if ($stable(s, @(posedge clk))) st++;
      if ($changed(s, @(posedge clk))) ch++;
      if ($sampled(c) == 5) begin
        pg = $past(c, 2, 1'b1, @(posedge clk));
        note($past(c));
      end
    end
  cover property (@(posedge clk) c == 4) begin pcov = $past(c); end
  chk k(clk, c);
  initial begin
    #100;
    if (r1 != 1 || f1 != 1 || st != 8 || ch != 2) begin
      $error("rose %0d fell %0d stable %0d changed %0d", r1, f1, st, ch); errors++;
    end
    if (pg != 3 || pt != 4 || pcov != 3 || k.pchk != 4) begin
      $error("past %0d %0d %0d %0d", pg, pt, pcov, k.pchk); errors++;
    end
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
