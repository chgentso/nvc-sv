// $value$plusargs in packages, class methods, tasks and initial blocks
// Expected values: IEEE 1800-2017 21.6 ($value$plusargs returns nonzero if found and stores the converted
// value, returns 0 and leaves the variable unchanged otherwise; %d accepts '_' separators).
// Run with +FOO=7 +N=1_000 (see the testlist line).
package p;
  class c;
    function int args();
      int v;
      if (!$value$plusargs("FOO=%d", v)) v = -1;
      return v;
    endfunction
  endclass
  const c obj = new();
endpackage
module svplusinit1;
  task t(output int r);
    int y;
    if ($value$plusargs("FOO=%d", y)) r = y; else r = -2;
  endtask
  int errs = 0;
  initial begin
    int x, r, z, n;
    x = -9; n = -9;
    if (!$value$plusargs("FOO=%d", x)) begin $error("FOO not found"); errs++; end
    else if (x !== 7) begin $error("x=%0d expected 7", x); errs++; end
    t(r);
    if (r !== 7) begin $error("task r=%0d expected 7", r); errs++; end
    if (p::obj.args() !== 7) begin $error("package class method %0d expected 7", p::obj.args()); errs++; end
    if (!$value$plusargs("N=%d", n)) begin $error("N not found"); errs++; end
    else if (n !== 1000) begin $error("n=%0d expected 1000", n); errs++; end
    z = 5;
    if ($value$plusargs("MISSING=%d", z)) begin $error("MISSING found"); errs++; end
    if (z !== 5) begin $error("z changed to %0d", z); errs++; end
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
