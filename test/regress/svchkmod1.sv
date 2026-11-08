// IEEE 1800-2017 17.2, 17.3: a checker may be declared inside a module and
// instantiated there; a checker formal with a default value acts as a
// parameter when given an elaboration-time constant (here the delay of
// ##N), and the default is used when the actual is omitted.  A checker may
// also be instantiated in procedural code (17.3, 16.14.6): its concurrent
// assertions then behave as procedural concurrent assertions, attempted
// only when the procedure reaches the instance, with $inferred_clock as
// the clock of the enclosing always procedure (16.14.7).
module svchkmod1;
  logic clk = 0, a = 0, b = 0, en = 0;
  int n_fail_d1 = 0, n_fail_d2 = 0;
  int n_cov_p = 0, n_fail_p = 0;
  int exp_fail_d1 = 0, exp_fail_d2 = 0, exp_cov_p = 0, exp_fail_p = 0;

  checker chk_delay(logic x, logic y, event ck, int N = 1, int ID = 0);
    a1: assert property (@ck x |-> ##N y)
      else begin
        if (ID == 1) svchkmod1.n_fail_d1++;
        else svchkmod1.n_fail_d2++;
      end
  endchecker

  checker chk_proc(logic x, logic y, event ck = $inferred_clock);
    a1: assert property (@ck x |-> y) else svchkmod1.n_fail_p++;
    c1: cover property (@ck x) svchkmod1.n_cov_p++;
  endchecker

  chk_delay d1(a, b, posedge clk, , 1);       // N defaults to 1
  chk_delay d2(a, b, posedge clk, 2, 2);      // N = 2

  always @(posedge clk)
    if (en) chk_proc cp(a, b);

  always #5 clk = ~clk;                       // posedge at 5, 15, ...

  // Stimulus at negedges; reference model on values sampled at posedges
  logic sa[$], sb[$], sen[$];
  always @(posedge clk) begin
    sa.push_back(a); sb.push_back(b); sen.push_back(en);
  end

  // Edge:        1  2  3  4  5  6  7  8  9  10
  bit va[10]  = '{1, 0, 0, 1, 1, 0, 1, 0, 0, 0};
  bit vb[10]  = '{0, 1, 0, 1, 0, 1, 0, 0, 1, 0};
  bit ven[10] = '{0, 1, 1, 1, 1, 0, 1, 1, 0, 0};

  initial begin
    for (int k = 0; k < 10; k++) begin
      a = va[k]; b = vb[k]; en = ven[k];
      @(negedge clk);
    end
    repeat (3) @(negedge clk);

    for (int k = 0; k < sa.size(); k++) begin
      if (sa[k] && k + 1 < sa.size() && !sb[k + 1]) exp_fail_d1++;
      if (sa[k] && k + 2 < sa.size() && !sb[k + 2]) exp_fail_d2++;
      if (sen[k] && sa[k]) exp_cov_p++;
      if (sen[k] && sa[k] && !sb[k]) exp_fail_p++;
    end
    if (exp_fail_d1 == 0 || exp_fail_d2 == 0 || exp_cov_p == 0
        || exp_fail_p == 0)
      $fatal(1, "degenerate stimulus");

    if (n_fail_d1 != exp_fail_d1)
      $fatal(1, "N=1 failures %0d, expected %0d", n_fail_d1, exp_fail_d1);
    if (n_fail_d2 != exp_fail_d2)
      $fatal(1, "N=2 failures %0d, expected %0d", n_fail_d2, exp_fail_d2);
    if (n_cov_p != exp_cov_p)
      $fatal(1, "procedural cover %0d, expected %0d", n_cov_p, exp_cov_p);
    if (n_fail_p != exp_fail_p)
      $fatal(1, "procedural failures %0d, expected %0d", n_fail_p,
             exp_fail_p);
    $display("PASSED");
    $finish;
  end
endmodule
