// A final deferred assertion (assert final) outside a procedure, and a final procedure, at the end of simulation.
// Expected values: IEEE 1800-2017 16.4 (deferred assertions; a module-level one behaves as in an always_comb,
// so it is evaluated at time 0 and whenever c changes, and its action block runs for each failure),
// 9.2.3 (final), 9.4.2 (posedges at 5 and 15 give c=2 at 22), 20.2 ($finish runs final procedures).
// The else action of an assert final runs in the Postponed region, where a variable may not be written
// (4.4.2.9), and is a single subroutine call (16.4), so it is a $display; the three failures, one for
// each of c=0,1,2, are checked with the gold file.
// The end of simulation is an explicit $finish, as what $stop does in a batch run is not defined by
// the LRM (20.2).
`timescale 1ns/1ns
module svfinal5;
  logic clk = 0;
  always #5 clk = ~clk;
  int c = 0;
  always @(posedge clk) c++;
  initial #22 $finish;
  assert final (c == 99) else $display("F fail c=%0d", c);
  final begin
    if (c !== 2) $error("c must be 2, got %0d", c);
    else $display("PASSED");
  end
endmodule
