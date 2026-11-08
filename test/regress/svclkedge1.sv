// Clocking block with input skews (default #1, #3, #1step), edge-qualified
// input and output skews, output skews #0 and the default #2, driven from
// a procedure after @(cb).  Counters of different periods are sampled and a
// monitor samples the six outputs every 1 ns (at x.5).
// Expected values: IEEE 1800-2017 14.3 (skews), 14.4 (input sampling: #N is
// the value N time units before the clocking event, #1step the value in the
// preponed region of the event), 14.16 (synchronous drives: executed at the
// clocking event, applied at the event plus the output skew; an output edge
// skew applies the drive at that edge of the clock following the event).
// Clock edges: posedge at 5, 15, ..., negedge at 10, 20, ...; events k=1..6
// occur at 5 + 10*(k-1) and cb.o* <= k is executed at each of them.
//   a   (default input #1)  = floor(T-1 / 10)           a steps at each negedge
//   b   (#3)                = b at T-3 (steps every 3 ns; if a step coincides
//                             with the sampling instant either value is accepted)
//   c   (#1step)            = value before T (steps every 7 ns)
//   oa  (default #2)        = k at event + 2
//   ob  (#0)                = k at the event
//   oc  (negedge)           = k at the next negedge: event + 5
//   of  (negedge #1)        = k at event + 6
//   od  (posedge), og (posedge #2): the edge named is the edge of the clocking
//                             event itself: k at the event (+2 for og), or, if the drive
//                             waits for the next such edge, one clock period later;
//                             either schedule is accepted
// The sampled inputs with an edge skew (d, e, f, g) must be values that the
// signal held during the clock period before the event; their exact sampling
// instant is not checked.
`timescale 1ns/1ps
module svclkedge1;
  logic clk = 0;
  logic [3:0] a = 0, b = 0, c = 0, d = 0, e = 0;
  logic [3:0] oa, ob, oc, od, oe, of, og, f = 0, g = 0;
  int cnt = 0;
  int errors = 0;
  always #5 clk = ~clk;
  clocking cb @(posedge clk);
    default input #1 output #2;
    input  a;
    input  #3 b;
    input  #1step c;
    output oa;
    output #0 ob;
    output negedge oc;
    output posedge od;
    output negedge #1 of;
    output posedge #2 og;
    input negedge #1 f;
    input posedge #1 g;
    input  posedge d;
    input  negedge e;
  endclocking
  always @(negedge clk) begin a <= a + 1; end
  always #3 b = b + 1;
  always #7 c = c + 1;
  always #4 d = d + 1;
  always #6 e = e + 1;
  always #2 f = f + 1;
  always #3 g = g + 1;

  // monitor of the outputs, times in units of 0.5 ns
  int ms[$];
  logic [3:0] ma[$], mb[$], mc[$], md[$], mf[$], mg[$];
  always begin
    #0.5;
    ma.push_back(oa); mb.push_back(ob); mc.push_back(oc);
    md.push_back(od); mf.push_back(of); mg.push_back(og);
    ms.push_back(int'($realtime * 2));
    #0.5;
  end

  // value of a counter that steps every P ns (from 0 at time 0) at time tau
  // (in 0.5 ns units); the value just after the step at tau and just before
  function automatic logic [3:0] cnt_after(int P, int tau2);
    return logic'(tau2 / (2 * P)) % 16;
  endfunction
  function automatic logic [3:0] cnt_before(int P, int tau2);
    if (tau2 > 0 && tau2 % (2 * P) == 0) return ((tau2 / (2 * P)) - 1) % 16;
    return (tau2 / (2 * P)) % 16;
  endfunction
  // the sampled value must be held by the counter at some instant of the
  // period before event time T (T2 in half ns)
  function automatic bit plausible(logic [3:0] v, int P, int T2);
    for (int t2 = (T2 > 20 ? T2 - 20 : 0); t2 <= T2; t2++)
      if (v === cnt_after(P, t2) || v === cnt_before(P, t2)) return 1;
    return 0;
  endfunction

  task automatic chk(string what, int k, logic [3:0] got, logic [3:0] exp);
    if (got !== exp) begin $error("event %0d: %s = %0d expected %0d", k, what, got, exp); errors++; end
  endtask

  initial begin
    repeat (6) begin
      int T2;
      @(cb);
      T2 = int'($realtime * 2);
      cnt++;
      // plain skews, exactly
      chk("a", cnt, cb.a, (T2 / 2 - 1) / 10);
      begin
        automatic logic [3:0] bx = cb.b;
        if (bx !== cnt_after(3, T2 - 6) && bx !== cnt_before(3, T2 - 6)) begin
          $error("event %0d: b = %0d, counter held %0d/%0d at T-3", cnt, bx,
                 cnt_before(3, T2 - 6), cnt_after(3, T2 - 6));
          errors++;
        end
      end
      chk("c", cnt, cb.c, cnt_before(7, T2));
      // edge skews: plausibility
      if (!plausible(cb.d, 4, T2)) begin $error("event %0d: d = %0d implausible", cnt, cb.d); errors++; end
      if (!plausible(cb.e, 6, T2)) begin $error("event %0d: e = %0d implausible", cnt, cb.e); errors++; end
      if (!plausible(cb.f, 2, T2)) begin $error("event %0d: f = %0d implausible", cnt, cb.f); errors++; end
      if (!plausible(cb.g, 3, T2)) begin $error("event %0d: g = %0d implausible", cnt, cb.g); errors++; end
      cb.oa <= cnt;
      cb.ob <= cnt;
      cb.oc <= cnt;
      cb.od <= cnt;
      cb.of <= cnt;
      cb.og <= cnt;
    end
    #40;
    // outputs: value k from the drive instant on, x before the first drive
    begin
      automatic int nbad = 0, bad_od0 = 0, bad_od1 = 0, bad_og0 = 0, bad_og1 = 0;
      for (int i = 0; i < ms.size(); i++) begin
        automatic int t2 = ms[i];
        logic [3:0] ea, eb, ec, ef, od0, od1, og0, og1;
        ea = 'x; eb = 'x; ec = 'x; ef = 'x; od0 = 'x; od1 = 'x; og0 = 'x; og1 = 'x;
        for (int k = 1; k <= 6; k++) begin
          automatic int ev2 = 2 * (5 + 10 * (k - 1));     // event time in half ns
          if (t2 >= ev2 + 4)  ea = k;           // +2
          if (t2 >= ev2)      eb = k;           // +0
          if (t2 >= ev2 + 10) ec = k;           // next negedge
          if (t2 >= ev2 + 12) ef = k;           // next negedge + 1
          if (t2 >= ev2)      od0 = k;          // posedge edge skew: the event itself ...
          if (t2 >= ev2 + 20) od1 = k;          // ... or the next posedge
          if (t2 >= ev2 + 4)  og0 = k;          // +2 from the event ...
          if (t2 >= ev2 + 24) og1 = k;          // ... or from the next posedge
        end
        if (ma[i] !== ea || mb[i] !== eb || mc[i] !== ec || mf[i] !== ef) begin
          if (nbad < 5)
            $error("t=%0d.%0d oa=%0d ob=%0d oc=%0d of=%0d expected %0d %0d %0d %0d",
                   t2 / 2, (t2 % 2) * 5, ma[i], mb[i], mc[i], mf[i], ea, eb, ec, ef);
          nbad++;
        end
        if (md[i] !== od0) bad_od0++;
        if (md[i] !== od1) bad_od1++;
        if (mg[i] !== og0) bad_og0++;
        if (mg[i] !== og1) bad_og1++;
      end
      if (nbad != 0) errors++;
      // an edge skew naming the edge of the clocking event itself: applied at that
      // event or at the following one (the LRM does not say which when they coincide)
      if (bad_od0 != 0 && bad_od1 != 0) begin $error("od matches neither schedule (%0d / %0d bad samples)", bad_od0, bad_od1); errors++; end
      if (bad_og0 != 0 && bad_og1 != 0) begin $error("og matches neither schedule (%0d / %0d bad samples)", bad_og0, bad_og1); errors++; end
      if (ms.size() < 90) begin $error("only %0d output samples", ms.size()); errors++; end
    end
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
