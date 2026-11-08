// Covergroups embedded in a class, with and without sample() arguments, with a cross
// Expected values: IEEE 1800-2017 19.4 (covergroups in classes), 19.5, 19.5.1 (bins[]),
// 19.6 (cross; 5 x 2 = 10 bins), 19.8.1 (sample arguments), 19.11 (coverage = average of
// the items of the group, weight 1; the default bin is not counted)
module svcovclass1;
  typedef enum {RD, WR} dir_t;
  int errs = 0;
  function automatic void chkr(string m, real got, real exp);
    if (got > exp + 1.0e-6 || got < exp - 1.0e-6) begin
      $error("%s: got %f expected %f", m, got, exp); errs++;
    end
  endfunction
  class item;
    bit [3:0] adr;
    bit [1:0] sel;
    dir_t dir;
  endclass
  class collector;
    item it;
    int hits;
    covergroup cg_item;
      cp_adr: coverpoint it.adr { bins regs[] = {[0:3]}; bins hi = {[4:15]}; }
      cp_dir: coverpoint it.dir;
      cp_sel: coverpoint it.sel { bins full = {3}; bins partial = default; }
      x_adr_dir: cross cp_adr, cp_dir;
    endgroup
    covergroup cg_arg with function sample(bit [3:0] a, bit w);
      cp_a: coverpoint a { bins lo = {[0:7]}; bins hi = {[8:15]}; }
      cp_w: coverpoint w;
    endgroup
    function new();
      cg_item = new();
      cg_arg = new();
    endfunction
    function void write(item t);
      it = t;
      cg_item.sample();
      cg_arg.sample(t.adr, t.dir == WR);
      hits++;
    endfunction
    // adr: 5 bins, dir: 2, sel: 1 (default excluded), cross: 10; cg_arg: 2 + 2
    function void report(int h, real item_c, real arg_c, real adr_c, real cross_c);
      string m = $sformatf("hits=%0d", hits);
      if (hits != h) begin $error("%s expected %0d hits", m, h); errs++; end
      chkr({m, " item"}, cg_item.get_inst_coverage(), item_c);
      chkr({m, " arg"}, cg_arg.get_inst_coverage(), arg_c);
      chkr({m, " adr"}, cg_item.cp_adr.get_inst_coverage(), adr_c);
      chkr({m, " cross"}, cg_item.x_adr_dir.get_inst_coverage(), cross_c);
    endfunction
  endclass
  initial begin
    automatic collector c = new;
    automatic item t = new;
    c.report(0, 0.0, 0.0, 0.0, 0.0);
    t.adr = 1; t.dir = RD; t.sel = 3; c.write(t);
    c.report(1, (20.0 + 50.0 + 100.0 + 10.0) / 4, 50.0, 20.0, 10.0);
    t.adr = 9; t.dir = WR; t.sel = 0; c.write(t);
    c.report(2, (40.0 + 100.0 + 100.0 + 20.0) / 4, 100.0, 40.0, 20.0);
    t.adr = 3; t.dir = WR; t.sel = 1; c.write(t);
    c.report(3, (60.0 + 100.0 + 100.0 + 30.0) / 4, 100.0, 60.0, 30.0);
    t.adr = 15; t.dir = RD; t.sel = 2; c.write(t);
    c.report(4, (60.0 + 100.0 + 100.0 + 40.0) / 4, 100.0, 60.0, 40.0);
    if (errs == 0) $display("PASSED");
  end
endmodule
