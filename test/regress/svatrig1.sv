// Sequence event control (@s) and the .triggered method of named sequences
// with a clocking event in the sequence declaration.
// Expected values: IEEE 1800-2017 16.13.5 / 16.13.6 (a sequence instance
// reaches its end point at a clock tick when its expression matches the
// values sampled at ticks; @s and wait(s.triggered) are released in that time
// step, and s.triggered is false in every other time step: it is reset at the
// start of the next time step), 16.9.3 (sampling at the clock tick).
// A reference model records the sampled values at each posedge and computes
// the match ticks of  a ##1 b ##1 c  and  b[*2] ##1 c  itself; the times at
// which @s1 fires and at which wait(s2.triggered) returns must be equal to
// those ticks.  The value of .triggered read in the active region of the tick
// itself (before the observed region) is not checked.
module svatrig1;
  logic clk = 0;
  logic a = 0, b = 0, c = 0;
  logic [15:0] lfsr = 16'h1D2B;
  always #5 clk = ~clk;
  always @(negedge clk) begin
    lfsr <= {lfsr[14:0], lfsr[15] ^ lfsr[13] ^ lfsr[12] ^ lfsr[10]};
    a <= lfsr[0] | lfsr[3];
    b <= lfsr[1] & lfsr[4];
    c <= lfsr[2] | lfsr[5];
  end

  sequence s1;
    @(posedge clk) a ##1 b ##1 c;
  endsequence
  sequence s2;
    @(posedge clk) b[*2] ##1 c;
  endsequence

  int errors = 0;
  int tick = 0;
  bit sa[$], sb[$], sc[$];
  int ev1[64], wt1[64], wt2[64];
  int nev1 = 0, nwt1 = 0, nwt2 = 0;

  // reference model: sampled values at every posedge
  always @(posedge clk) begin
    sa.push_back(a); sb.push_back(b); sc.push_back(c);
    tick++;
  end

  always @s1 begin ev1[nev1] = $time; nev1++; end
  always @(negedge clk)
    if (s1.triggered || s2.triggered) begin
      $error("%0t: triggered true at a negedge time step", $time); errors++;
    end
  initial forever begin
    wait (s2.triggered);
    wt2[nwt2] = $time; nwt2++;
    #1;
    if (s2.triggered) begin $error("%0t: s2.triggered still true one time unit later", $time); errors++; end
    wait (!s2.triggered);
  end
  initial forever begin
    wait (s1.triggered);
    wt1[nwt1] = $time; nwt1++;
    #1;
    wait (!s1.triggered);
  end

  task automatic cmp(string what, int got[64], int n, int exp[$]);
    bit ok = (n == exp.size());
    for (int k = 0; ok && k < n; k++) if (got[k] != exp[k]) ok = 0;
    if (!ok) begin
      $error("%s: %0d events, expected %0d (%p)", what, n, exp.size(), exp);
      for (int k = 0; k < n; k++) $display("  got %0d", got[k]);
      errors++;
    end
  endtask

  initial begin
    #300;
    begin
      int m1[$], m2[$];
      for (int k = 0; k < sa.size(); k++) begin
        if (k >= 2 && sa[k-2] && sb[k-1] && sc[k]) m1.push_back(5 + 10 * k);
        if (k >= 2 && sb[k-2] && sb[k-1] && sc[k]) m2.push_back(5 + 10 * k);
      end
      if (m1.size() == 0 || m2.size() == 0)
        $display("note: stimulus produced no match (%0d / %0d)", m1.size(), m2.size());
      cmp("@s1", ev1, nev1, m1);
      cmp("wait(s1.triggered)", wt1, nwt1, m1);
      cmp("wait(s2.triggered)", wt2, nwt2, m2);
    end
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
