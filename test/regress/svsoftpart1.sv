// Expected values: IEEE 1800-2017 18.5 (constraint blocks, with-block constraints are hard),
// 18.5.13 (soft constraints), 18.5.4 (dist weights: values are always inside the listed ranges),
// 18.5.10 (solve before does not change the solution space)
class cfg_c;
  rand int unsigned dmin, dmax, emin, emax;
  rand bit zero;
  constraint cmin { soft dmin == 0; }
  constraint cmax { solve zero before dmax;
    if (zero) { dmax == 0; } else { dmax dist {[1:10] :/ 1, [11:50] :/ 4}; } }
  constraint emin_c { soft emin == 0; }
  constraint emax_c { solve zero before emax;
    if (zero) { emax == 0; } else { emax dist {[1:10] :/ 1, [11:50] :/ 4}; } }
endclass
module svsoftpart1;
  cfg_c c = new();
  int errs = 0;
  initial begin
    repeat (3) begin
      if (!c.randomize() with { zero == 0; emin == 1; emax == 2; }) begin
        $error("randomize failed"); errs++;
      end
      // no conflict: the soft constraint on dmin holds
      if (c.dmin !== 0) begin $error("dmin=%0d, expected 0", c.dmin); errs++; end
      // zero == 0 selects the dist branch: dmax in [1:50]
      if (!(c.dmax >= 1 && c.dmax <= 50)) begin $error("dmax=%0d outside 1..50", c.dmax); errs++; end
      // hard with-block constraints override the soft emin == 0
      if (c.emin !== 1) begin $error("emin=%0d, expected 1", c.emin); errs++; end
      if (c.emax !== 2) begin $error("emax=%0d, expected 2", c.emax); errs++; end
      if (c.zero !== 0) begin $error("zero=%0d, expected 0", c.zero); errs++; end
    end
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
