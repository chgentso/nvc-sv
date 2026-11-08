// Bin construction: bins[N] over value lists, auto_bin_max, enum auto bins, overlapping
// bins, at_least and weight options
// Expected values: IEEE 1800-2017 19.5 (bins; a value in several bins counts in each),
// 19.5.1 (bins[N]: the first N-1 bins take floor(M/N) values each, the last bin takes the
// rest; automatic bins: min(2^M, auto_bin_max), enum: one per member), 19.7 (option.at_least,
// option.weight; group options are the default of the items), 19.11 (coverage arithmetic)
module svcovbins2;
  bit [3:0] v; bit [7:0] u; bit [3:0] y;
  typedef enum {A, B=3, C, D} e_t;
  e_t e;
  int errs = 0;
  covergroup g1;
    cp1: coverpoint v { bins b[3] = {[0:9]}; }
    cp2: coverpoint v { bins b[4] = {1,3,5,7,9,11,13,15,2}; }
    cp3: coverpoint u { option.auto_bin_max = 4; }
    cp4: coverpoint v { option.auto_bin_max = 3; }
    cp5: coverpoint e;
    cp6: coverpoint y { bins x[] = {[1:2],3}; }

  endgroup
  g1 g = new;
  covergroup g2;
    cp1: coverpoint v { bins a = {1}; bins b = {1,2}; }
  endgroup
  g2 g2i = new;
  covergroup g3;
    option.at_least = 2;
    cp1: coverpoint v { bins a = {1}; bins b = {2}; option.weight = 3; }
    cp2: coverpoint y { bins a = {1}; option.at_least = 1; }
    cp3: coverpoint u { bins a = {1}; option.weight = 2; }
  endgroup
  g3 g3i = new;
  function automatic void chkr(string m, real got, real exp);
    if (got > exp + 1.0e-6 || got < exp - 1.0e-6) begin
      $error("%s: got %f expected %f", m, got, exp); errs++;
    end
  endfunction
  initial begin
    // g1: probe each coverpoint after sampling single values
    for (int k = 0; k < 16; k++) begin
      g1 gi; gi = new;
      v = k; u = k*16; e = A; gi.sample();
      // cp1: {0-2},{3-5},{6-9}
      chkr($sformatf("v=%0d cp1", k), gi.cp1.get_inst_coverage(), k <= 9 ? 100.0/3 : 0.0);
      // cp2: {1,3},{5,7},{9,11},{13,15,2}
      chkr($sformatf("v=%0d cp2", k), gi.cp2.get_inst_coverage(), ((k % 2) == 1 || k == 2) ? 25.0 : 0.0);
      // cp3: 4 automatic bins of 64 values, one hit
      chkr($sformatf("v=%0d cp3", k), gi.cp3.get_inst_coverage(), 25.0);
      // cp4: 3 automatic bins over 16 values, one hit
      chkr($sformatf("v=%0d cp4", k), gi.cp4.get_inst_coverage(), 100.0/3);
      // cp5: 4 enum members
      chkr($sformatf("v=%0d cp5", k), gi.cp5.get_inst_coverage(), 25.0);
    end
    for (int k = 0; k < 4; k++) begin
      g1 gi; gi = new;
      e = k == 0 ? A : k == 1 ? B : k == 2 ? C : D; y = k+1; gi.sample();
      chkr($sformatf("e=%0d cp5", e), gi.cp5.get_inst_coverage(), 25.0);
      // cp6: three bins for the values 1, 2, 3
      chkr($sformatf("y=%0d cp6", y), gi.cp6.get_inst_coverage(), k < 3 ? 100.0/3 : 0.0);
    end
    // g2: a value that lies in two bins hits both
    v = 1; g2i.sample();
    chkr("g2.1", g2i.get_inst_coverage(), 100.0);
    v = 2; g2i.sample();
    chkr("g2.2", g2i.get_inst_coverage(), 100.0);
    // g3: u is still 240 from the loop, so cp3 never hits
    v = 1; y = 1; g3i.sample();
    chkr("g3.1", g3i.get_inst_coverage(), (3*0.0 + 1*100.0 + 2*0.0) / 6);
    chkr("g3.1a", g3i.cp1.get_inst_coverage(), 0.0);
    chkr("g3.1b", g3i.cp2.get_inst_coverage(), 100.0);
    chkr("g3.1c", g3i.cp3.get_inst_coverage(), 0.0);
    g3i.sample();
    chkr("g3.2", g3i.get_inst_coverage(), (3*50.0 + 1*100.0 + 2*0.0) / 6);
    chkr("g3.2a", g3i.cp1.get_inst_coverage(), 50.0);
    v = 2; g3i.sample(); g3i.sample();
    chkr("g3.3", g3i.get_inst_coverage(), (3*100.0 + 1*100.0 + 2*0.0) / 6);
    chkr("g3.3a", g3i.cp1.get_inst_coverage(), 100.0);
    if (errs == 0) $display("PASSED");
  end
endmodule
