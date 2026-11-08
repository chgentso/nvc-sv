// Classes with covergroups declared in a parameterised interface
// Expected values: IEEE 1800-2017 19.4, 19.5, 19.7 (option.weight, option.name,
// option.per_instance), 19.11 (coverage = weighted average of the coverpoints; both
// weights are 1 here), 8.25 (parameterised class members), 25.3 (interface members)
// Each interface instance checks its own numbers, so no order between instances is assumed.
interface cgif #(parameter int W = 4, parameter bit EN = 1) (input logic clk);
  int errs = 0;
  bit done = 0;
  function automatic void chkr(string m, real got, real exp);
    if (got > exp + 1.0e-6 || got < exp - 1.0e-6) begin
      $error("W=%0d %s: got %f expected %f", W, m, got, exp); errs++;
    end
  endfunction
  class base;
    int n;
  endclass
  class der extends base;
    covergroup cg (string name) with function sample(int x);
      option.per_instance = 1;
      option.name = name;
      cp: coverpoint x { option.weight = W > 1; bins lo = {[0:3]}; bins hi = {[4:7]}; }
      cp2: coverpoint x { option.weight = EN; bins one = {1}; bins other = {[2:15]}; }
    endgroup
    function new();
      cg = new("c");
    endfunction
    function void smp(int x); cg.sample(x); endfunction
    function real cov(); return cg.get_inst_coverage(); endfunction
  endclass
  der d;
  initial begin
    d = new();
    d.smp(2); d.smp(5);
    chkr("1", d.cov(), (100.0 + 50.0) / 2);
    d.smp(1);
    chkr("2", d.cov(), 100.0);
    done = 1;
  end
endinterface
module svcovclassiface1;
  logic clk = 0;
  cgif #(8) i1(clk);
  cgif #(3, 1) i2(clk);
  initial begin
    #1;
    if (!i1.done || !i2.done) $error("interface initial blocks did not run");
    else if (i1.errs == 0 && i2.errs == 0) $display("PASSED");
  end
endmodule
