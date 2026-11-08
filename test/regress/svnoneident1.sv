`default_nettype none
// A variable named none and a bin named none in a coverpoint, with `default_nettype none in effect
// Expected values: IEEE 1800-2017 22.8 (`default_nettype none only turns off implicit nets; "none" is an ordinary
// identifier otherwise), 19.5 (bins have their own scope), 19.11 (get_inst_coverage: the default bin is not counted,
// so the covered bin `none` gives 100%)
module svnoneident1;
  logic clk = 0;
  logic [1:0] v;
  int none = 3;
  int fails = 0;
  covergroup cg @(posedge clk);
    cp: coverpoint v { bins none = {0}; bins other = default; }
  endgroup
  cg c = new;
  initial begin
    real cov;
    if (none != 3) begin $error("none = %0d", none); fails++; end
    v = 0; #1 clk = 1; #1 clk = 0;
    v = 1; #1 clk = 1; #1 clk = 0;
    cov = c.get_inst_coverage();
    if (cov < 99.99 || cov > 100.01) begin $error("coverage = %f", cov); fails++; end
    if (fails == 0) $display("PASSED");
  end
endmodule
