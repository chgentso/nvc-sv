// IEEE 1800-2017 14.14: a global clocking declaration names the global
// clocking event, referred to as $global_clock in assertions and as the
// clocking argument of sampled value functions (16.9.3).  A module without
// its own global clocking finds the one of an enclosing instance.  The
// global clocking sampled value functions (16.9.4) $rose_gclk,
// $changed_gclk, $past_gclk and the future functions $changing_gclk and
// $future_gclk are evaluated on the global clock.
module svglobclk2_sub(input logic a, input logic b);
  // No global clocking here: $global_clock is the one of the parent
  a1: assert property (@($global_clock) a |=> b) else svglobclk2.n_a1++;
endmodule

module svglobclk2;
  logic fclk = 0, a = 0, b = 0;
  int n_a1 = 0, n_rose = 0, n_rose_g = 0, n_chg_g = 0, n_past = 0;
  int n_changing = 0, n_fut = 0, n_rose_fn = 0;

  global clocking gclk @(posedge fclk); endclocking

  always #5 fclk = ~fclk;

  svglobclk2_sub u(a, b);

  c_rose: cover property (@($global_clock) $rose(a)) n_rose++;
  c_rose_g: cover property (@($global_clock) $rose_gclk(a)) n_rose_g++;
  c_chg_g: cover property (@($global_clock) $changed_gclk(b)) n_chg_g++;
  c_past: cover property (@($global_clock) $past_gclk(a) && b) n_past++;
  c_changing: cover property (@($global_clock) $changing_gclk(a))
                n_changing++;
  c_fut: cover property (@($global_clock) !a && $future_gclk(a)) n_fut++;

  always @(posedge fclk)
    if ($rose(a, @($global_clock))) n_rose_fn++;

  // Stimulus at negedges; sampled values at posedges for the model
  bit va[12] = '{0, 1, 1, 0, 1, 0, 0, 1, 1, 1, 0, 0};
  bit vb[12] = '{0, 0, 1, 0, 0, 1, 1, 0, 1, 0, 0, 0};
  bit sa[$], sb[$];
  always @(posedge fclk) begin sa.push_back(a); sb.push_back(b); end

  initial begin
    int e_a1, e_rose, e_chg, e_past, e_changing, e_fut;
    foreach (va[k]) begin
      a = va[k]; b = vb[k];
      @(negedge fclk);
    end
    @(negedge fclk);

    e_a1 = 0; e_rose = 0; e_chg = 0; e_past = 0; e_changing = 0; e_fut = 0;
    foreach (sa[k]) begin
      // Before the first edge the sampled values are the initial ones (0)
      automatic bit pa = (k == 0) ? 1'b0 : sa[k - 1];
      automatic bit pb = (k == 0) ? 1'b0 : sb[k - 1];
      if (k + 1 < sa.size()) begin
        if (sa[k] && !sb[k + 1]) e_a1++;
        if (sa[k] != sa[k + 1]) e_changing++;
        if (!sa[k] && sa[k + 1]) e_fut++;
      end
      if (!pa && sa[k]) e_rose++;
      if (pb != sb[k]) e_chg++;
      if (pa && sb[k]) e_past++;
    end
    if (e_a1 == 0 || e_rose == 0 || e_chg == 0 || e_past == 0)
      $fatal(1, "degenerate stimulus");

    if (n_a1 != e_a1) $fatal(1, "a1 failures %0d, expected %0d", n_a1, e_a1);
    if (n_rose != e_rose) $fatal(1, "$rose %0d, expected %0d", n_rose, e_rose);
    if (n_rose_g != e_rose)
      $fatal(1, "$rose_gclk %0d, expected %0d", n_rose_g, e_rose);
    if (n_rose_fn != e_rose)
      $fatal(1, "$rose(a, @($global_clock)) %0d, expected %0d", n_rose_fn,
             e_rose);
    if (n_chg_g != e_chg)
      $fatal(1, "$changed_gclk %0d, expected %0d", n_chg_g, e_chg);
    if (n_past != e_past)
      $fatal(1, "$past_gclk %0d, expected %0d", n_past, e_past);
    if (n_changing != e_changing)
      $fatal(1, "$changing_gclk %0d, expected %0d", n_changing, e_changing);
    if (n_fut != e_fut)
      $fatal(1, "$future_gclk %0d, expected %0d", n_fut, e_fut);
    $display("PASSED");
    $finish;
  end
endmodule
