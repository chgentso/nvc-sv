// Self-checking test of reversed bin ranges, e.g. {[10:2]}.
// Expected values: IEEE 1800-2017 19.5 and 11.4.13 (a range [a:b] with a > b contains no values), so the
// bin can never be hit, whatever value is sampled; 19.11 (coverage = covered bins / bins, so 0 here).
module svcovrev1;
  bit [31:0] v;
  covergroup cg1; coverpoint v { bins high = {[(2**24)+1 : 0]}; } endgroup
  covergroup cg2; coverpoint v { bins b = {[10 : 2]}; } endgroup
  covergroup cg3; coverpoint v { bins b = {[5 : 0]}; } endgroup
  cg1 a = new; cg2 b = new; cg3 c = new;
  int fails = 0;
  task automatic try(bit [31:0] x);
    cg1 a1; cg2 b1; cg3 c1; real r1, r2, r3;
    a1 = new; b1 = new; c1 = new;
    v = x; a1.sample(); b1.sample(); c1.sample();
    r1 = a1.get_inst_coverage(); r2 = b1.get_inst_coverage(); r3 = c1.get_inst_coverage();
    if (r1 != 0.0 || r2 != 0.0 || r3 != 0.0) begin
      fails++;
      $error("%0d: reversed range bin was hit: %0.2f %0.2f %0.2f", x, r1, r2, r3);
    end
  endtask
  initial begin
    try(32'hAA000000); try(20000000); try(16777217); try(0); try(3); try(5); try(6); try(11); try(32'hFFFFFFFF);
    if (fails == 0) $display("PASSED");
  end
endmodule
