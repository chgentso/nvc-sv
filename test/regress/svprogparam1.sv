// IEEE 1800-2017 24.3: a program declaration may have a parameter port
// list (#(...)) and ports, and is instantiated like a module with
// parameter value assignments by name or by position (23.10).  Type
// parameters are allowed too.  When every program has ended, the
// simulation finishes (24.7).
program svprogparam1_p #(parameter int N = 2, parameter string TAG = "d",
                         parameter type T = byte)
  (input logic clk, output int total);
  T acc;
  initial begin
    acc = 0;
    total = 0;
    repeat (N) begin
      @(posedge clk);
      acc++;
      total += N;
    end
    svprogparam1.report(TAG, total, $bits(T));
  end
endprogram

module svprogparam1;
  logic clk = 0;
  int t1, t2, t3;
  int seen = 0;
  string tags = "";

  function void report(string tag, int total, int w);
    seen++;
    tags = {tags, tag};
    case (tag)
      "d": if (total != 4 || w != 8) $fatal(1, "default: %0d %0d", total, w);
      "x": if (total != 9 || w != 16)
             $fatal(1, "named override: %0d %0d", total, w);
      "y": if (total != 25 || w != 32)
             $fatal(1, "positional override: %0d %0d", total, w);
      default: $fatal(1, "bad tag %s", tag);
    endcase
  endfunction

  always #5 clk = ~clk;

  svprogparam1_p p0(.clk, .total(t1));
  svprogparam1_p #(.N(3), .TAG("x"), .T(shortint)) p1(.clk, .total(t2));
  svprogparam1_p #(5, "y", int) p2(clk, t3);

  initial #1000 $fatal(1, "programs did not end the simulation");

  // All programs end at 45 (fifth posedge of p2), which ends the
  // simulation (24.7) and runs this final procedure.
  final begin
    if ($time != 45) $fatal(1, "simulation ended at %0t", $time);
    if (seen != 3) $fatal(1, "only %0d programs reported", seen);
    if (p1.N != 3 || p2.TAG != "y") $fatal(1, "hierarchical parameter");
    if (t1 != 4 || t2 != 9 || t3 != 25)
      $fatal(1, "totals %0d %0d %0d", t1, t2, t3);
    $display("PASSED");
  end
endmodule
