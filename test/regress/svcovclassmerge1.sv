// Covergroups embedded in classes are per object; type coverage of several instances
// Expected values: IEEE 1800-2017 19.4 (an embedded covergroup is instantiated by new
// in the constructor, one instance per object), 19.8 (get_inst_coverage is the coverage
// of the instance it is called on), 19.7 (type_option.merge_instances defaults to 0:
// get_coverage() is the average of the instance coverages), 19.11 ($get_coverage).
// Left out as implementation-defined: which covergroup types $get_coverage averages over,
// and whether the covergroup type of a module is shared by all instances of the module.
class C;
  int v;
  covergroup cg;
    option.per_instance = 1;
    coverpoint v { bins b0 = {0}; bins b1 = {1}; bins b2 = {2}; bins b3 = {3}; }
  endgroup
  function new(string n = "");
    cg = new();
    if (n != "") cg.set_inst_name(n);
  endfunction
  function void s(int x); v = x; cg.sample(); endfunction
endclass

class D;
  int v;
  covergroup cg;
    coverpoint v { bins b0 = {0}; bins b1 = {1}; bins b2 = {2}; bins b3 = {3}; }
  endgroup
  function new(); cg = new(); endfunction
  function void s(int x); v = x; cg.sample(); endfunction
endclass

class P;
  int v;
  covergroup cg(int base);
    coverpoint v { bins b0 = {base}; bins b1 = {base + 1}; bins b2 = {base + 2}; }
  endgroup
  function new(int b); cg = new(b); endfunction
  function void s(int x); v = x; cg.sample(); endfunction
endclass

class Q;
  int v, w;
  covergroup cga; coverpoint v { bins b0 = {0}; bins b1 = {1}; bins b2 = {2}; bins b3 = {3}; } endgroup
  covergroup cgb; coverpoint w { bins b0 = {0}; bins b1 = {1}; bins b2 = {2}; bins b3 = {3}; } endgroup
  function new(); cga = new; cgb = new; endfunction
  function void s(int x); v = x; w = 3 - x; cga.sample(); cgb.sample(); endfunction
endclass

module m;
  int v;
  covergroup cg;
    coverpoint v { bins b0 = {0}; bins b1 = {1}; bins b2 = {2}; bins b3 = {3}; }
  endgroup
  cg c1 = new, c2 = new;
  task s1(int x); v = x; c1.sample(); endtask
  task s2(int x); v = x; c2.sample(); endtask
endmodule

module svcovclassmerge1;
  C c1, c2, c3, c4;
  D d1, d2;
  P p1, p2, p3;
  Q q1, q2;
  m m1(), m2();
  int errs = 0;
  function automatic void chkr(string msg, real got, real exp);
    if (got > exp + 1.0e-6 || got < exp - 1.0e-6) begin
      $error("%s: got %f expected %f", msg, got, exp); errs++;
    end
  endfunction
  function automatic void chkg(string msg, real got);
    if (!(got >= 0.0 && got <= 100.0)) begin
      $error("%s: $get_coverage %f outside 0..100", msg, got); errs++;
    end
  endfunction
  initial begin
    c1 = new("A"); c2 = new("B"); c3 = new("A"); c4 = new;
    d1 = new; d2 = new;
    chkr("$get_coverage 0", $get_coverage(), 0.0);
    c1.s(0);
    chkr("C1 c1", c1.cg.get_inst_coverage(), 25.0);
    chkr("C1 c2", c2.cg.get_inst_coverage(), 0.0);
    chkr("C1 c3", c3.cg.get_inst_coverage(), 0.0);
    chkr("C1 c4", c4.cg.get_inst_coverage(), 0.0);
    c2.s(1); c3.s(2);
    chkr("C2 c1", c1.cg.get_inst_coverage(), 25.0);
    chkr("C2 c4", c4.cg.get_inst_coverage(), 0.0);
    // instances 25, 25, 25, 0
    chkr("C2 type", c1.cg.get_coverage(), (25.0 + 25.0 + 25.0 + 0.0) / 4);
    chkg("after C", $get_coverage());
    d1.s(0); d2.s(0);
    chkr("D d1", d1.cg.get_inst_coverage(), 25.0);
    chkr("D d2", d2.cg.get_inst_coverage(), 25.0);
    chkr("D type", d1.cg.get_coverage(), 25.0);
    chkg("after D", $get_coverage());

    p1 = new(0); p2 = new(10);
    p1.s(0);
    chkr("P1 p1", p1.cg.get_inst_coverage(), 100.0/3);
    chkr("P1 p2", p2.cg.get_inst_coverage(), 0.0);
    p2.s(10); p2.s(11); p2.s(12);
    chkr("P2 p1", p1.cg.get_inst_coverage(), 100.0/3);
    chkr("P2 p2", p2.cg.get_inst_coverage(), 100.0);
    p3 = new(0);
    p3.s(1);
    chkr("P3 p1", p1.cg.get_inst_coverage(), 100.0/3);
    chkr("P3 p2", p2.cg.get_inst_coverage(), 100.0);
    chkr("P3 p3", p3.cg.get_inst_coverage(), 100.0/3);
    chkr("P3 type", p1.cg.get_coverage(), (100.0/3 + 100.0 + 100.0/3) / 3);

    q1 = new; q2 = new;
    q1.s(0); q2.s(1);
    chkr("Q q1.cga", q1.cga.get_inst_coverage(), 25.0);
    chkr("Q q2.cga", q2.cga.get_inst_coverage(), 25.0);
    chkr("Q q1.cgb", q1.cgb.get_inst_coverage(), 25.0);
    chkr("Q q2.cgb", q2.cgb.get_inst_coverage(), 25.0);

    m1.s1(0); m1.s2(1); m2.s1(2);
    chkr("M m1.c1", m1.c1.get_inst_coverage(), 25.0);
    chkr("M m1.c2", m1.c2.get_inst_coverage(), 25.0);
    chkr("M m2.c1", m2.c1.get_inst_coverage(), 25.0);
    chkr("M m2.c2", m2.c2.get_inst_coverage(), 0.0);
    chkg("MT1", m1.c1.get_coverage());
    chkg("MT2", m2.c2.get_coverage());
    chkg("final", $get_coverage());
    if (errs == 0) $display("PASSED");
  end
endmodule
