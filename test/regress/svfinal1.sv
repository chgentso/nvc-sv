// Final procedures in a top module, in instances of a module and in an interface; $finish at 35 runs them all.
// Expected values: IEEE 1800-2017 9.2.3 (final procedures run once at the end of simulation),
// 9.4.1 (delays), 20.2 ($finish), 21.2 ($display). The order of the four final procedures is
// implementation-defined, so only a completion counter is used to print PASSED after the last one.
`timescale 1ns/1ns
package svfinal1_pkg;
  int done = 0;
  int errs = 0;
  function automatic void chk(bit ok, string m);
    if (!ok) begin $error("%s", m); errs++; end
  endfunction
  function automatic void fin();
    done++;
    if (done == 4 && errs == 0) $display("PASSED");
  endfunction
endpackage

module sub(input int id);
  import svfinal1_pkg::*;
  int cnt = 0;
  always #10 cnt++;
  final begin
    chk(cnt == 3, "sub: cnt (increments at 10, 20, 30) must be 3");
    chk($time == 35, "sub: $time in final must be 35");
    fin();
  end
endmodule

interface ifc;
  import svfinal1_pkg::*;
  int x = 3;
  final begin
    chk(x == 9, "ifc: x must be 9");
    fin();
  end
endinterface

module svfinal1;
  import svfinal1_pkg::*;
  int v = 1;
  ifc i();
  sub s1(1), s2(2);
  initial begin #35 v = 7; i.x = 9; $display("finishing"); $finish; end
  final begin
    chk(v == 7, "top: v must be 7");
    chk(s1.cnt == 3 && s2.cnt == 3, "top: s1.cnt and s2.cnt must be 3");
    fin();
  end
endmodule
