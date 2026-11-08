// Expected values: IEEE 1800-2017 24.7 (program control tasks / implicit
// $finish when all programs have completed), 24.3, 9.2.3 (final blocks), 20.4.
// Posedges of clk (period 10, first edge at 5) occur at 5,15,25,35...
// Program p1 (n=2) completes at 15, p2 (n=4) completes at 35; at that point
// every program has finished so the simulation ends implicitly, before the
// module initial block (#100) wakes up.  Final blocks execute at the end.
// (nvc note: parameter port lists on programs are not accepted, so unused here.)
`timescale 1ns/1ps
int errs, finals;
int done_t[2];
bit alive;
int edges;
function automatic void fin(string who);
  finals++;
  if (finals == 3) begin
    if (errs == 0) $display("PASSED");
  end
endfunction
program prog (input logic clk, input int n);
  initial begin
    repeat (n) @(posedge clk);
    done_t[n/2-1] = $time;
  end
  final begin
    automatic int exp_t = (n == 2) ? 15 : 35;
    if (done_t[n/2-1] != exp_t) begin
      $error("program %0d body finished at %0d, expected %0d", n, done_t[n/2-1], exp_t);
      errs++;
    end
    if ($time != 35) begin
      $error("program %0d final at %0d, expected 35", n, $time);
      errs++;
    end
    fin("p");
  end
endprogram
module svprogfinish1;
  logic clk = 0; always #5 clk = ~clk;
  always @(posedge clk) edges++;
  initial begin #100 alive = 1; $finish; end
  prog p1 (clk, 2);
  prog p2 (clk, 4);
  final begin
    if (alive) begin $error("module initial ran after all programs ended"); errs++; end
    if (edges != 4) begin $error("edges = %0d, expected 4", edges); errs++; end
    fin("m");
  end
endmodule
