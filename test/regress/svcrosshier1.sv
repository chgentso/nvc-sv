// Self-checking test of a covergroup embedded in a class whose coverpoints are hierarchical expressions
// (class handle -> virtual interface -> signal) that are crossed with plain coverpoints.
// Expected values: IEEE 1800-2017 19.5 (a coverpoint expression may be any expression), 19.6 (cross items
// are coverpoints or variables, so the hierarchical operands get explicit coverpoints cp_key and cp_bat),
// 19.11 (covered bins / bins per item; group = average of the items).
// Each of cp_k, cp_b, cp_key, cp_bat has two automatic bins and both are hit (samples (0,0) and (1,1));
// each cross has 4 bins and two are hit, so the group coverage is (4*100 + 50 + 50) / 6.
interface vif_t;
  logic key0_in = 0;
  logic bat = 0;
endinterface
class cfg_c;
  virtual vif_t vif;
endclass
class seq;
  cfg_c cfg;
  bit cp_in;
  covergroup cg;
    cp_k: coverpoint cp_in;
    cp_b: coverpoint cp_in;
    cp_key: coverpoint cfg.vif.key0_in;
    cp_bat: coverpoint cfg.vif.bat;
    k_x: cross cp_k, cp_key;
    b_x: cross cp_b, cp_bat;
  endgroup
  function new(cfg_c c);
    cfg = c;
    cg = new;
  endfunction
  int fails = 0;
  function void chk(string what, real got, real exp);
    if (got < exp - 0.05 || got > exp + 0.05) begin
      fails++; $error("%s: got %0.1f, expected %0.1f", what, got, exp);
    end
  endfunction
  task go(virtual vif_t v);
    cp_in = 0; cg.sample();
    v.key0_in = 1; v.bat = 1; cp_in = 1; cg.sample();
    chk("group", cg.get_inst_coverage(), (4 * 100.0 + 50.0 + 50.0) / 6);
    chk("k_x", cg.k_x.get_inst_coverage(), 50.0);
    chk("b_x", cg.b_x.get_inst_coverage(), 50.0);
    chk("cp_key", cg.cp_key.get_inst_coverage(), 100.0);
    chk("cp_bat", cg.cp_bat.get_inst_coverage(), 100.0);
    if (fails == 0) $display("PASSED");
  endtask
endclass
module svcrosshier1;
  vif_t v();
  cfg_c cfg = new;
  initial begin
    seq s;
    cfg.vif = v;
    s = new(cfg);
    s.go(v);
  end
endmodule
