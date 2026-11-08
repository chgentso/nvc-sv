// Self-checking: "inside" over arrays held in another object (state variables of the constraint).
// Expected values: IEEE 1800-2017 18.5.3 (set membership with an array), 18.5 (constraints).
module svcrvstate3;
  class vcfg;
    rand int unsigned legal[$];
    rand bit [3:0] fixed[3];
  endclass
  class stream;
    rand int unsigned eew;
    rand bit [3:0] pick;
    vcfg cfg;
    constraint c1 { eew inside {cfg.legal}; }
    constraint c2 { pick inside {cfg.fixed}; }
  endclass
  initial begin
    automatic stream s = new;
    automatic int bad = 0;
    s.cfg = new;
    s.cfg.legal = '{8, 16, 64};
    s.cfg.fixed = '{2, 5, 9};
    repeat (40) begin
      if (!s.randomize()) bad++;
      else if (!(s.eew inside {8, 16, 64}) || !(s.pick inside {2, 5, 9})) bad++;
    end
    if (bad != 0) $error("bad=%0d", bad);
    else $display("PASSED");
  end
endmodule
