// Clocking block with an inout clockvar, default input #1step / output #1:
// synchronous drives (the last of two drives to the same clockvar in one
// cycle wins), sampling of an inout clockvar, and a cycle-delayed drive
// (cb.io <= ##2 value).
// Expected values: IEEE 1800-2017 14.3 (default skews), 14.4 (input
// sampling with #1step), 14.11 (cycle delay ##N on a synchronous drive:
// N clocking events later), 14.16 (synchronous drives, last drive wins).
// Clock edges at 5, 15, 25, 35, 45 ns.  Schedule:
//   t=0  cb.io <= 3 and (after @(cb) at 5) cb.io <= 5, cb.s <= 1: both
//        target the event at 5, io = 5 and s = 1 at 5 + 1 = 6
//   t=7  cb.io <= 6 (mid cycle): applied at 15 + 1 = 16
//   t=15 sampled io = 5 (value before the edge), cb.io <= ##2 9: applied at
//        the event 2 cycles later (35) + 1 = 36
// A monitor samples {s,io} every 1 ns at x.5 and logs the changes.
`timescale 1ns/1ps
module svclockdrive3;
  bit clk;
  logic [3:0] io;
  logic s;
  logic [4:0] prev = 5'bx;
  int errors = 0;
  clocking cb @(posedge clk);
    default input #1step output #1;
    inout io;
    output s;
  endclocking
  initial forever #5 clk = ~clk;

  int t_log[$];            // time in units of 0.1 ns
  logic [4:0] v_log[$];
  initial begin
    #0.5;
    forever begin
      if ({s, io} !== prev) begin
        t_log.push_back(int'($realtime * 10));
        v_log.push_back({s, io});
        prev = {s, io};
      end
      #1;
    end
  end

  initial begin
    cb.io <= 4'd3;
    @(cb);
    cb.s <= 1;
    cb.io <= 4'd5;
    #2 cb.io <= 4'd6;
    @(cb);
    if (cb.io !== 4'd5) begin $error("sampled io=%0d expected 5", cb.io); errors++; end
    cb.io <= ##2 4'd9;
    #30;     // until 45
    begin
      automatic int exp_t[3] = '{65, 165, 365};
      automatic logic [4:0] exp_v[3] = '{5'b1_0101, 5'b1_0110, 5'b1_1001};
      if (t_log.size() != 3) begin
        $error("%0d changes seen, expected 3", t_log.size()); errors++;
        foreach (t_log[i]) $display("  %0d %b", t_log[i], v_log[i]);
      end else
        for (int k = 0; k < 3; k++)
          if (t_log[k] != exp_t[k] || v_log[k] !== exp_v[k]) begin
            $error("change %0d: %0d (x0.1 ns) %b expected %0d %b", k, t_log[k], v_log[k], exp_t[k], exp_v[k]);
            errors++;
          end
    end
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
