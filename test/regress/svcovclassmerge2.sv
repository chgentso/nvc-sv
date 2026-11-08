// Several covergroups embedded in one class: value bins, transition bins, at_least, cross
// Expected values: IEEE 1800-2017 19.4 (one instance per object), 19.8 (get_inst_coverage is
// per instance), 19.5 (value bins), 19.5.2 (transition bins), 19.7 (option.at_least), 19.6
// (cross of coverpoints with disjoint values has no hits), 19.11 (coverage arithmetic)
class A;
  int v;
  covergroup cgv;
    cp: coverpoint v { bins b0 = {0}; bins b1 = {1}; bins b2 = {2}; bins b3 = {3}; }
  endgroup
  covergroup cgt;
    cp: coverpoint v { bins t01 = (0 => 1); bins t12 = (1 => 2); bins t23 = (2 => 3); bins t30 = (3 => 0); }
  endgroup
  covergroup cgd;
    cp: coverpoint v { bins b0 = {0}; bins b1 = {1}; bins b2 = {2}; bins b3 = {3}; option.at_least = 2; }
  endgroup
  covergroup cgx;
    a: coverpoint v { bins b0 = {0}; bins b1 = {1}; }
    b: coverpoint v { bins c0 = {2}; bins c1 = {3}; }
    x: cross a, b;
  endgroup
  function new(); cgv = new; cgt = new; cgd = new; cgx = new; endfunction
  function void s(int x); v = x; cgv.sample(); cgt.sample(); cgd.sample(); cgx.sample(); endfunction
endclass
module svcovclassmerge2;
  A a1, a2;
  int errs = 0;
  function automatic void chkr(string m, real got, real exp);
    if (got > exp + 1.0e-6 || got < exp - 1.0e-6) begin
      $error("%s: got %f expected %f", m, got, exp); errs++;
    end
  endfunction
  task automatic chk4(string m, A o, real e1, real e2, real e3, real e4);
    chkr({m, " cgv"}, o.cgv.get_inst_coverage(), e1);
    chkr({m, " cgt"}, o.cgt.get_inst_coverage(), e2);
    chkr({m, " cgd"}, o.cgd.get_inst_coverage(), e3);
    chkr({m, " cgx"}, o.cgx.get_inst_coverage(), e4);
  endtask
  initial begin
    a1 = new; a2 = new;
    a1.s(0); a1.s(1); a1.s(2);
    // a1: values 0,1,2 of 4; transitions 0=>1, 1=>2 of 4; no bin reaches 2 hits;
    // cgx: a 2/2, b 1/2, cross 0/4
    chk4("a1", a1, 75.0, 50.0, 0.0, (100.0 + 50.0 + 0.0) / 3);
    chk4("a2", a2, 0.0, 0.0, 0.0, 0.0);
    a2.s(2); a2.s(3); a2.s(0); a2.s(1);
    chk4("a1 again", a1, 75.0, 50.0, 0.0, (100.0 + 50.0 + 0.0) / 3);
    // a2: all 4 values; transitions 2=>3, 3=>0, 0=>1; cgx: a 2/2, b 2/2, cross 0/4
    chk4("a2 again", a2, 100.0, 75.0, 0.0, (100.0 + 100.0 + 0.0) / 3);
    if (errs == 0) $display("PASSED");
  end
endmodule
