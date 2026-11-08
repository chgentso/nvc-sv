// Soft constraints on elements of a 2-D array of rand objects.
// Expected values: IEEE 1800-2017 18.5.3 (foreach with multiple indices c[ch, f]), 18.5.10 (soft
// constraints hold when satisfiable), 18.6.3: lo = defaults[f] + ch, i.e. {10,20,30} + ch
class cfg;
  rand int lo;
endclass
class env;
  rand cfg c[2][3];
  int defaults[3] = '{10, 20, 30};
  function new(); foreach (c[i, j]) c[i][j] = new(); endfunction
  constraint k_c {
    foreach (c[ch, f]) {
      soft c[ch][f].lo == defaults[f] + ch;
    }
  }
endclass
module svrandobjarr2;
  env e = new();
  initial begin
    if (!e.randomize()) $error("randomize failed");
    if (e.c[0][0].lo !== 10 || e.c[0][2].lo !== 30 || e.c[1][1].lo !== 21 || e.c[1][2].lo !== 31)
      $error("%0d %0d %0d %0d", e.c[0][0].lo, e.c[0][2].lo, e.c[1][1].lo, e.c[1][2].lo);
    else $display("PASSED");
  end
endmodule
