// Expected values: IEEE 1800-2017 11.4.13 (inside with real range, real value, string items)
module svinsidescal1;
  string name;
  int errs = 0;
  initial begin
    real r;
    r = 16.0;
    if (!(r inside {[8:1024]})) begin errs++; $error("16.0 in [8:1024]"); end
    r = 4.5;
    if (r inside {[8:1024]}) begin errs++; $error("4.5 not in [8:1024]"); end
    r = 8.5;
    if (!(r inside {[8:1024], 3})) begin errs++; $error("8.5 in list"); end
    if (!(r inside {8.5})) begin errs++; $error("8.5 in {8.5}"); end

    name = "x0";
    if (!(name inside {"zero", "x0"})) begin errs++; $error("x0 in list"); end
    name = "q";
    if (name inside {"zero", "x0"}) begin errs++; $error("q not in list"); end
    if (errs == 0) $display("PASSED");
  end
endmodule
