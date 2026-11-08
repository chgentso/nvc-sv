// Covergroup embedded in a parameterised class
// Expected values: IEEE 1800-2017 19.4, 19.5 (bins, default bin), 19.11 (coverage =
// covered/total bins, default bin not counted; group = average of its coverpoints)
module svcovclass2;
  int errs = 0;
  function automatic void chkr(string m, real got, real exp);
    if (got > exp + 1.0e-6 || got < exp - 1.0e-6) begin
      $error("%s: got %f expected %f", m, got, exp); errs++;
    end
  endfunction
  class base; endclass
  class coll #(type T = int, int W = 4) extends base;
    T val;
    bit [W-1:0] bv;
    covergroup cg;
      cp: coverpoint bv { bins lo = {[0:3]}; bins hi = {[4:$]}; }
      cp2: coverpoint val { bins z = {0}; bins nz = default; }
    endgroup
    function new(); cg = new; endfunction
    function void put(bit [W-1:0] x, T v); bv = x; val = v; cg.sample(); endfunction
  endclass
  initial begin
    automatic coll #(int, 3) c = new;
    // cp: bins lo and hi; cp2: one counted bin (z)
    c.put(1, 0);
    chkr("1.group", c.cg.get_inst_coverage(), (50.0 + 100.0) / 2);
    chkr("1.cp", c.cg.cp.get_inst_coverage(), 50.0);
    c.put(5, 7);
    chkr("2.group", c.cg.get_inst_coverage(), 100.0);
    chkr("2.cp", c.cg.cp.get_inst_coverage(), 100.0);
    if (errs == 0) $display("PASSED");
  end
endmodule
