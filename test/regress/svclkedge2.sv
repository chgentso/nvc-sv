// Clocking blocks on a negedge and on both edges of the clock, with
// edge-qualified input and output skews on signals of different periods.
// Expected values: IEEE 1800-2017 14.3 (skews, default input skew #1step),
// 14.4 (input sampling), 14.16 (synchronous drives: a drive is applied at the
// clocking event or at the next one plus skew; the last of several drives
// applied at the same instant wins; an output edge skew applies the drive at
// that edge of the clock, plus the delay).
// cbn @(negedge clk): events at 10, 20, ..., 60 are used, k = 1..6;
//   oy (output posedge)     = k at the next posedge, event + 5
//   oz (output negedge #2)  = k at the event + 2, or one period later: the edge
//                             named is the edge of the clocking event itself
//   z  (default #1step)     = value before the event (z steps every 7 ns)
// cba @(clk): events at 5, 10, ..., 40 (every edge), k = 1..8;
//   ow (output negedge), oi (output posedge #1): applied at the next edge of
//   the given kind, counting the issuing event itself or not (accepted both
//   ways, consistently over the run); of two drives applied at one instant the
//   later one wins.
// Sampled inputs with an edge skew (x, y, w, cba.z) must be values the signal
// held during the preceding clock period (exact instant not checked).
// A monitor samples oy, oz, ow, oi every 1 ns (x.5).
`timescale 1ns/1ps
module svclkedge2;
  logic clk = 0;
  logic [3:0] x = 0, y = 0, z = 0, w = 0;
  logic [3:0] oy, oz, ow, oi;
  logic [3:0] io = 0;
  int cnt = 0, cnt2 = 0;
  int errors = 0;
  always #5 clk = ~clk;
  always #3 x = x + 1;
  always #4 y = y + 1;
  always #7 z = z + 1;
  always #2 w = w + 1;
  clocking cbn @(negedge clk);
    input posedge x;
    input negedge #1 y;
    input z;
    output posedge oy;
    output negedge #2 oz;
  endclocking
  clocking cba @(clk);
    input posedge w;
    input negedge z;
    output negedge ow;
    output posedge #1 oi;
  endclocking

  // monitor, times in half ns
  int ms[$];
  logic [3:0] my[$], mz[$], mw[$], mi[$];
  always begin
    #0.5;
    my.push_back(oy); mz.push_back(oz); mw.push_back(ow); mi.push_back(oi);
    ms.push_back(int'($realtime * 2));
    #0.5;
  end

  // counter stepping every P ns from 0 at time 0; value just before time tau2/2
  function automatic logic [3:0] cnt_before(int P, int tau2);
    if (tau2 > 0 && tau2 % (2 * P) == 0) return ((tau2 / (2 * P)) - 1) % 16;
    return (tau2 / (2 * P)) % 16;
  endfunction
  function automatic logic [3:0] cnt_after(int P, int tau2);
    return (tau2 / (2 * P)) % 16;
  endfunction
  function automatic bit plausible(logic [3:0] v, int P, int T2);
    for (int t2 = (T2 > 20 ? T2 - 20 : 0); t2 <= T2; t2++)
      if (v === cnt_after(P, t2) || v === cnt_before(P, t2)) return 1;
    return 0;
  endfunction

  // drives: issue time (half ns), value; apply time computed by the caller.
  // value of the clockvar at time t2: the drive with the largest apply time
  // <= t2, the later issued one on a tie; x before any.
  function automatic logic [3:0] value_at(int t2, int n, int apply2[16]);
    logic [3:0] v = 'x;
    int best = -1;
    for (int k = 1; k <= n; k++)
      if (apply2[k] <= t2 && apply2[k] >= best) begin best = apply2[k]; v = k; end
    return v;
  endfunction

  // next edge at or after (strict = 0) / after (strict = 1) time e2 (half ns);
  // posedges at 10 + 20 j, negedges at 20 + 20 j in half-ns units
  function automatic int next_edge(int e2, bit pos, bit strict);
    int t2 = pos ? 10 : 20;
    while (strict ? (t2 <= e2) : (t2 < e2)) t2 += 20;
    return t2;
  endfunction

  initial begin
    repeat (6) begin
      int T2;
      @(cbn);
      T2 = int'($realtime * 2);
      cnt++;
      if (!plausible(cbn.x, 3, T2)) begin $error("N%0d: x = %0d implausible", cnt, cbn.x); errors++; end
      if (!plausible(cbn.y, 4, T2)) begin $error("N%0d: y = %0d implausible", cnt, cbn.y); errors++; end
      if (cbn.z !== cnt_before(7, T2)) begin
        $error("N%0d: z = %0d expected %0d (value before the event)", cnt, cbn.z, cnt_before(7, T2));
        errors++;
      end
      cbn.oy <= cnt;
      cbn.oz <= cnt;
    end
    #30;
  end
  int iss2[16];       // issue time of the cba drive k, half ns
  initial begin
    repeat (8) begin
      int T2;
      @(cba);
      T2 = int'($realtime * 2);
      cnt2++;
      iss2[cnt2] = T2;
      if (!plausible(cba.w, 2, T2)) begin $error("A%0d: w = %0d implausible", cnt2, cba.w); errors++; end
      if (!plausible(cba.z, 7, T2)) begin $error("A%0d: z = %0d implausible", cnt2, cba.z); errors++; end
      cba.ow <= cnt2;
      cba.oi <= cnt2;
    end
  end

  initial begin
    #90;
    begin
      int ap_y[16], ap_z0[16], ap_z1[16];
      int ap_w0[16], ap_w1[16], ap_i0[16], ap_i1[16];
      automatic int bad_y = 0, bad_z0 = 0, bad_z1 = 0, bad_w0 = 0, bad_w1 = 0, bad_i0 = 0, bad_i1 = 0;
      for (int k = 1; k <= 6; k++) begin
        automatic int ev2 = 2 * (10 * k);                      // cbn event k: negedge at 10k
        ap_y[k]  = next_edge(ev2, 1, 1);             // next posedge (strictly after a negedge)
        ap_z0[k] = ev2 + 4;                          // negedge #2: at the event
        ap_z1[k] = ev2 + 20 + 4;                     // or one period later
      end
      for (int k = 1; k <= 8; k++) begin
        automatic int e2 = iss2[k];
        ap_w0[k] = next_edge(e2, 0, 0);              // negedge: counting the event itself
        ap_w1[k] = next_edge(e2, 0, 1);              // strictly after
        ap_i0[k] = next_edge(e2, 1, 0) + 2;          // posedge #1
        ap_i1[k] = next_edge(e2, 1, 1) + 2;
      end
      for (int i = 0; i < ms.size(); i++) begin
        automatic int t2 = ms[i];
        if (my[i] !== value_at(t2, 6, ap_y))  bad_y++;
        if (mz[i] !== value_at(t2, 6, ap_z0)) bad_z0++;
        if (mz[i] !== value_at(t2, 6, ap_z1)) bad_z1++;
        if (mw[i] !== value_at(t2, 8, ap_w0)) bad_w0++;
        if (mw[i] !== value_at(t2, 8, ap_w1)) bad_w1++;
        if (mi[i] !== value_at(t2, 8, ap_i0)) bad_i0++;
        if (mi[i] !== value_at(t2, 8, ap_i1)) bad_i1++;
      end
      if (bad_y != 0) begin $error("oy: %0d samples differ from the schedule", bad_y); errors++; end
      if (bad_z0 != 0 && bad_z1 != 0) begin $error("oz matches neither schedule (%0d / %0d)", bad_z0, bad_z1); errors++; end
      if (bad_w0 != 0 && bad_w1 != 0) begin $error("ow matches neither schedule (%0d / %0d)", bad_w0, bad_w1); errors++; end
      if (bad_i0 != 0 && bad_i1 != 0) begin $error("oi matches neither schedule (%0d / %0d)", bad_i0, bad_i1); errors++; end
      if (cnt != 6 || cnt2 != 8) begin $error("event counts %0d / %0d expected 6 / 8", cnt, cnt2); errors++; end
    end
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
