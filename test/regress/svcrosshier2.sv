// A hierarchical name as a cross item: IEEE 1800-2017 19.6 and A.2.11 allow
// only a coverpoint or a variable identifier.  With --relaxed nvc gives a
// warning and crosses an implicit coverpoint of the name.
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
    k_x: cross cp_k, cfg.vif.key0_in;
    b_x: cross cp_b, cfg.vif.bat;
  endgroup
  function new(cfg_c c);
    cfg = c;
    cg = new;
  endfunction
  task go(virtual vif_t v);
    cp_in = 0; cg.sample();
    v.key0_in = 1; v.bat = 1; cp_in = 1; cg.sample();
    $display("%0.1f", cg.get_inst_coverage());
    $display("%0.1f %0.1f", cg.k_x.get_inst_coverage(), cg.b_x.get_inst_coverage());
  endtask
endclass
module svcrosshier2;
  vif_t v();
  cfg_c cfg = new;
  initial begin
    seq s;
    cfg.vif = v;
    s = new(cfg);
    s.go(v);
  end
endmodule
