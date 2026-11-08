// Synchronous drives through a clocking block: output skews of 0, #2, #1,
// drives issued before the first clocking event, in the middle of a cycle
// (two drives of the same clockvar in one cycle: the last one wins), after a
// raw edge wait at the clocking event itself and after @(cb).
// Expected values: IEEE 1800-2017 14.3 (output skew), 14.16 (synchronous
// drives: a drive executed at the time of a clocking event takes effect at
// that event plus the output skew, a drive executed at any other time is
// applied at the next clocking event plus skew; with several drives to one
// clockvar in a cycle the last one wins).
// A monitor samples {a,b,c,d} every 1 ns at x.5 and records changes; the
// recorded list is compared with the schedule derived below.
`timescale 1ns/1ps
module svclockdrive1;
  bit clk;
  logic [3:0] prev;
  logic a, b, c, d;
  int errors = 0;
  clocking cb @(posedge clk);
    output a;
    output #2 b;
    output c;
    output #1 d;
  endclocking
  initial forever #5 clk = ~clk;       // posedges at 5, 15, 25, ...

  int  t_log[$];       // time in units of 0.1 ns
  logic [3:0] v_log[$];
  initial begin
    prev = 4'bz;
    #0.5;
    forever begin
      if ({a, b, c, d} !== prev) begin
        t_log.push_back(int'($realtime * 10));
        v_log.push_back({a, b, c, d});
        prev = {a, b, c, d};
      end
      #1;
    end
  end
  initial begin
    cb.a <= 1; cb.b <= 1;       // time 0, before first event: applied at 5 (a) and 7 (b)
    #2 cb.c <= 1;               // mid cycle: applied at 5
    @(posedge clk);             // 5: raw edge wait
    cb.d <= 1;                  // at the edge: applied at 5 + 1
    #1 cb.a <= 0;               // 6 mid: applied at 15
    #1 cb.a <= 1; cb.a <= 0;    // 7 mid, two drives: last wins, applied at 15
    @(cb);                      // 15
    cb.b <= 0; cb.c <= 0;       // at edge via cb: c at 15, b at 17
    #2 cb.d <= 0;               // 17 mid: applied at 25 + 1
    @(cb);                      // 25
    if (a !== 1'b0) begin $error("a at 25 is %b expected 0", a); errors++; end
    #12 cb.a <= 1;              // 37 mid: applied at 45
    #30;                        // 67
    begin
      // expected changes of {a,b,c,d}, observed at x.5
      automatic int exp_t[8] = '{5, 55, 65, 75, 155, 175, 265, 455};
      automatic logic [3:0] exp_v[8] = '{4'bxxxx, 4'b1x1x, 4'b1x11, 4'b1111, 4'b0101, 4'b0001, 4'b0000, 4'b1000};
      if (t_log.size() != 8) begin
        $error("%0d changes seen, expected 8", t_log.size()); errors++;
        foreach (t_log[i]) $display("  %0d.%0d %b", t_log[i] / 10, t_log[i] % 10, v_log[i]);
      end else
        for (int k = 0; k < 8; k++)
          if (t_log[k] != exp_t[k] || v_log[k] !== exp_v[k]) begin
            $error("change %0d: %0d (x0.1 ns) %b expected %0d %b", k, t_log[k], v_log[k], exp_t[k], exp_v[k]);
            errors++;
          end
    end
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
