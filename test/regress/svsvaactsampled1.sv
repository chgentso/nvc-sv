// Sampled value functions called in the pass and fail action blocks of
// concurrent assertions.
// IEEE 1800-2017 16.9.3: in an action block of a singly clocked assertion,
// $past, $rose, $fell, $stable, $changed and $sampled use the clock of the
// assertion (not the default clocking), and 16.14.1 / 16.9.3 the action
// block runs when the attempt ends, so the functions give the values of the
// sampled-value history at the clock tick where the attempt ends: $sampled is
// the value sampled at that tick (16.5.1, Preponed region), $past(e, n) the
// value n ticks before it, $past(e, n, g) the value at the n-th earlier tick
// at which the sampled gating expression g was true, and $rose / $fell /
// $stable / $changed compare the sampled value at that tick with the one at
// the previous tick (16.9.2).  The action block runs in the Reactive region,
// after the nonblocking updates of that time step, so the sampled values
// differ from the current ones there.
// A reference model records the sampled values (read in the Active region of
// each clock edge, before the nonblocking updates made at that edge) and each
// action block compares the functions with it.  Ticks with too little history
// for a function are not compared.  Pass actions of vacuous successes are
// not relied upon.
`timescale 1ns/1ns
module svsvaactsampled1;
  localparam int N = 20;
  localparam int dpat[N] = '{1, 2, 2, 3, 5, 5, 5, 6, 8, 9, 9, 10, 12, 12, 13, 14, 14, 14, 15, 17};
  localparam bit bpat[N] = '{0, 1, 1, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0};
  localparam bit gpat[N] = '{1, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 0, 1, 1, 0, 1, 0};
  localparam bit spat[N] = '{1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1};

  logic clk = 0;
  int d = 0;
  logic b = 0, g = 0, s = 0;
  int k = 0;

  default clocking dcb @(posedge clk); endclocking

  always #5 clk = ~clk;

  // New values at every rising edge, after the edge has been sampled
  always @(posedge clk) begin
    if (k < N) begin
      d <= dpat[k]; b <= bpat[k]; g <= gpat[k]; s <= spat[k];
    end
    k <= k + 1;
  end

  // Reference model: sampled values at each tick of each clock
  int pd[$], nd[$];
  bit pb[$], nb[$], pg[$], ng[$], ps[$], ns[$];
  always @(posedge clk) begin
    pd.push_back(d); pb.push_back(b); pg.push_back(g); ps.push_back(s);
  end
  always @(negedge clk) begin
    nd.push_back(d); nb.push_back(b); ng.push_back(g); ns.push_back(s);
  end

  int errors = 0, compared = 0;
  int npass1 = 0, nfail1 = 0, nact2 = 0, nfail3 = 0;
  int fail3_ticks[$];

  // Compares the values computed in an action block with the model at the
  // latest tick of the clock (neg = 0: rising edge, 1: falling edge)
  task automatic check(string tag, bit neg, int smp, int p1, int p2, int pg1,
                       bit rose, bit fell, bit stab, bit chg);
    int hd[$];
    bit hb[$], hg[$];
    int m, j;
    if (neg) begin hd = nd; hb = nb; hg = ng; end
    else begin hd = pd; hb = pb; hg = pg; end
    m = hd.size() - 1;
    compared++;
    if (smp !== hd[m]) begin
      $display("%s tick %0d: $sampled(d)=%0d expected %0d", tag, m, smp, hd[m]);
      errors++;
    end
    if (m >= 1) begin
      if (p1 !== hd[m-1]) begin
        $display("%s tick %0d: $past(d)=%0d expected %0d", tag, m, p1, hd[m-1]);
        errors++;
      end
      if (rose !== (hb[m] && !hb[m-1])) begin
        $display("%s tick %0d: $rose(b)=%0d", tag, m, rose); errors++;
      end
      if (fell !== (!hb[m] && hb[m-1])) begin
        $display("%s tick %0d: $fell(b)=%0d", tag, m, fell); errors++;
      end
      if (stab !== (hd[m] == hd[m-1])) begin
        $display("%s tick %0d: $stable(d)=%0d", tag, m, stab); errors++;
      end
      if (chg !== (hd[m] != hd[m-1])) begin
        $display("%s tick %0d: $changed(d)=%0d", tag, m, chg); errors++;
      end
    end
    if (m >= 2 && p2 !== hd[m-2]) begin
      $display("%s tick %0d: $past(d,2)=%0d expected %0d", tag, m, p2, hd[m-2]);
      errors++;
    end
    for (j = m - 1; j >= 0 && !hg[j]; j--) ;
    if (j >= 0 && pg1 !== hd[j]) begin
      $display("%s tick %0d: $past(d,1,g)=%0d expected %0d", tag, m, pg1, hd[j]);
      errors++;
    end
  endtask

  // Attempts end two ticks after they start when they pass, at the start
  // tick when s is false
  a1: assert property (s ##2 1) begin
    npass1++;
    check("a1 pass", 0, $sampled(d), $past(d), $past(d, 2), $past(d, 1, g),
          $rose(b), $fell(b), $stable(d), $changed(d));
  end else begin
    nfail1++;
    check("a1 fail", 0, $sampled(d), $past(d), $past(d, 2), $past(d, 1, g),
          $rose(b), $fell(b), $stable(d), $changed(d));
  end

  // Clocked on the falling edge: the action blocks use that clock, not the
  // default clocking
  a2: assert property (@(negedge clk) s) begin
    nact2++;
    check("a2 pass", 1, $sampled(d), $past(d), $past(d, 2), $past(d, 1, g),
          $rose(b), $fell(b), $stable(d), $changed(d));
  end else begin
    nact2++;
    check("a2 fail", 1, $sampled(d), $past(d), $past(d, 2), $past(d, 1, g),
          $rose(b), $fell(b), $stable(d), $changed(d));
  end

  // Fails one tick after it starts; an explicit clocking event argument
  // naming the same clock gives the same values
  a3: assert property (@(posedge clk) s |-> ##1 b)
  else begin
    nfail3++;
    fail3_ticks.push_back(pd.size() - 1);
    check("a3 fail", 0, $sampled(d),
          $past(d, 1, , @(posedge clk)), $past(d, 2), $past(d, 1, g, @(posedge clk)),
          $rose(b, @(posedge clk)), $fell(b, @(posedge clk)),
          $stable(d, @(posedge clk)), $changed(d, @(posedge clk)));
  end

  initial begin
    automatic int ep1 = 0, ef1 = 0, ef3 = 0;
    int ef3_ticks[$];
    #(10 * N + 2);
    if (pd.size() != N || nd.size() != N) begin
      $display("ticks %0d %0d expected %0d", pd.size(), nd.size(), N);
      errors++;
    end
    for (int t = 0; t < N; t++) begin
      if (t >= 2 && ps[t-2]) ep1++;
      if (!ps[t]) ef1++;
      if (t >= 1 && ps[t-1] && !pb[t]) begin ef3++; ef3_ticks.push_back(t); end
    end
    if (npass1 != ep1 || nfail1 != ef1) begin
      $display("a1: %0d passes %0d failures, expected %0d %0d", npass1, nfail1, ep1, ef1);
      errors++;
    end
    if (nact2 != N) begin
      $display("a2: %0d actions expected %0d", nact2, N); errors++;
    end
    if (fail3_ticks != ef3_ticks) begin
      $display("a3: failures at %p expected %p", fail3_ticks, ef3_ticks); errors++;
    end
    if (ef3 == 0 || ep1 == 0 || ef1 == 0) begin
      $display("model sanity"); errors++;
    end
    if (errors == 0 && compared > 0) $display("PASSED");
    $finish;
  end
endmodule
