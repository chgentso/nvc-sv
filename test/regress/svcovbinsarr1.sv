// IEEE 1800-2017 19.5.1: the values of a bin may be given by a set
// covergroup expression, an array or queue whose elements are the values
// (19.5.1.2): bins b[] creates one bin per element, a scalar bin one bin for
// all elements, and bins b[N] spreads the values over N bins.  The
// expression is evaluated when the covergroup is constructed.  A with
// clause (19.5.1.1) selects the values of a range list for which the with
// covergroup expression, using item and covergroup formal arguments, is
// true.  Coverage = covered bins / bins (19.11).
module svcovbinsarr1;
  bit [7:0] v;
  int darr[] = '{3, 5, 9, 200};
  int qv[$] = '{1, 2};
  bit [7:0] qv2[$] = '{100};

  covergroup cg1;
    coverpoint v { bins a[] = darr; }
  endgroup

  covergroup cg2;
    coverpoint v { bins q1 = qv; bins q2 = qv2; }
  endgroup

  covergroup cg3;
    coverpoint v { bins r[] = {[0:15]} with (item % 4 == 0); }
  endgroup

  covergroup cg4(int m);
    coverpoint v { bins r[] = {[0:31]} with (item % m == 1); }
  endgroup

  covergroup cg5;
    coverpoint v { bins f[2] = darr; }
  endgroup

  cg1 c1; cg2 c2; cg3 c3; cg4 c4; cg5 c5;

  task automatic chk(string what, real got, real exp);
    if (got < exp - 0.01 || got > exp + 0.01)
      $fatal(1, "%s: coverage %f, expected %f", what, got, exp);
  endtask

  task automatic smp(bit [7:0] x);
    v = x;
    c1.sample(); c2.sample(); c3.sample(); c4.sample(); c5.sample();
  endtask

  initial begin
    c1 = new; c2 = new; c3 = new; c4 = new(8); c5 = new;
    darr[1] = 77;                       // After construction: no effect
    qv.push_back(250);

    chk("cg1 none", c1.get_inst_coverage(), 0.0);
    smp(3);
    smp(9);
    smp(77);
    smp(250);
    // cg1: bins 3 5 9 200 -> 3 and 9 hit
    chk("cg1", c1.get_inst_coverage(), 50.0);
    // cg2: q1 = {1,2} not hit, q2 = {100} not hit
    chk("cg2 before", c2.get_inst_coverage(), 0.0);
    smp(2);
    chk("cg2", c2.get_inst_coverage(), 50.0);
    // cg3: bins 0 4 8 12 -> none of 3 9 77 250 2
    chk("cg3 before", c3.get_inst_coverage(), 0.0);
    smp(4); smp(13);
    chk("cg3", c3.get_inst_coverage(), 25.0);
    // cg4 (m = 8): bins 1 9 17 25 -> 9 hit
    chk("cg4", c4.get_inst_coverage(), 25.0);
    smp(25);
    chk("cg4 after 25", c4.get_inst_coverage(), 50.0);
    // cg5: f[0] = {3, 5}, f[1] = {9, 200}: both hit by 3 and 9
    chk("cg5", c5.get_inst_coverage(), 100.0);

    $display("PASSED");
  end
endmodule
