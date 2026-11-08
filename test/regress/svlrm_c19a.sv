module svlrm_c19a;
  // Self-checking test. Expected values: IEEE 1800-2017 19.3-19.5 (covergroups, coverpoints, bins,
  // bins with [] create one bin per value, enum coverpoints get one auto bin per member,
  // ignore_bins / illegal_bins / default bins are not counted, iff guards, transition bins),
  // 19.6 (cross coverage: cp_x x cp_e = 3 x 4 = 12 bins), 19.8.1 (sample() with arguments, ref
  // arguments), 19.9 (stop()/start(): no sampling while stopped), 19.11 (coverage computation:
  // coverage = hit bins / bins, covergroup = average of its coverpoints/crosses with equal weight),
  // 19.10 (get_inst_coverage, type get_coverage). Coverage is printed with %0.2f and compared exactly.
  //   cg1: cp_x 3/3, cp_e 4/4, cp_y 3/3 (default bin excluded), xc 4/12 -> (100+100+100+33.33)/4 = 83.33
  //   cg2: cp_v {a,b} 2/2 (sample (3,0) fails iff, 4 matches no bin), cp_t {1=>2, 2=>3=>4} 2/2 -> 100
  //   cg3: 2/2 -> 100
  typedef enum {A, B, C, D} e_t;
  bit [3:0] x, y; e_t e; bit clk = 0, en = 1;
  covergroup cg1 @(posedge clk);
    option.per_instance = 1;
    cp_x: coverpoint x { bins low = {[0:3]}; bins mid = {[4:7]}; bins high = {[8:15]}; }
    cp_e: coverpoint e;
    cp_y: coverpoint y { bins few[] = {1, 2, 3}; bins others = default; }
    xc: cross cp_x, cp_e;
  endgroup
  covergroup cg2 with function sample(bit [3:0] v, bit g);
    cp_v: coverpoint v iff (g) { bins a = {1}; bins b = {2, 3}; ignore_bins ig = {0}; illegal_bins ill = {15}; }
    cp_t: coverpoint v { bins t1 = (1 => 2); bins t2 = (2 => 3 => 4); }
  endgroup
  covergroup cg3 (ref bit [3:0] r, input int lim);
    cp: coverpoint r { bins lo = {[0:lim]}; bins hi = {[lim+1:15]}; }
  endgroup
  cg1 c1 = new; cg2 c2 = new; cg3 c3;
  bit [3:0] ref_v;
  int errs;
  function automatic void ck(bit ok, string m);
    if (!ok) begin errs++; $error("%s", m); end
  endfunction
  function automatic string f2(real r); return $sformatf("%0.2f", r); endfunction
  initial begin
    c3 = new(ref_v, 7);
    x = 0; e = A; y = 1; c1.sample();
    x = 5; e = B; y = 2; c1.sample();
    x = 9; e = C; y = 3; c1.sample();
    x = 1; e = D; y = 9; c1.sample();
    ck(f2(c1.get_inst_coverage()) == "83.33", "cg1 coverage");
    ck(f2(c1.cp_x.get_inst_coverage()) == "100.00", "cp_x");
    ck(f2(c1.cp_y.get_inst_coverage()) == "100.00", "cp_y (default bin not counted)");
    ck(f2(c1.cp_e.get_inst_coverage()) == "100.00", "cp_e");
    ck(f2(c1.xc.get_inst_coverage()) == "33.33", "cross 4 of 12");
    c2.sample(1, 1); c2.sample(2, 1); c2.sample(3, 0); c2.sample(4, 1); c2.sample(2, 1); c2.sample(3, 1); c2.sample(4, 1);
    ck(f2(c2.get_inst_coverage()) == "100.00", "cg2");
    ck(f2(c2.cp_v.get_inst_coverage()) == "100.00", "cp_v");
    ck(f2(c2.cp_t.get_inst_coverage()) == "100.00", "cp_t");
    ref_v = 3; c3.sample(); ref_v = 12; c3.sample();
    ck(f2(c3.get_inst_coverage()) == "100.00", "cg3");
    c2.stop(); c2.sample(15, 1);      // not sampled while stopped: the illegal bin is not hit
    ck(f2(c2.get_inst_coverage()) == "100.00", "cg2 after stop");
    c2.start();
    ck(f2(cg1::get_coverage()) == "83.33", "type coverage with a single instance");
    if (errs == 0) $display("PASSED");
  end
endmodule
