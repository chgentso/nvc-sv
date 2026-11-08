// $printtimescale, $timeunit and $timeprecision given a hierarchical name
// that does not name a module instance.
// IEEE 1800-2017 20.4.1 / 20.4.2: the argument is the hierarchical name of a
// module instance (or other scope); the LRM does not say what happens when
// there is no such scope.  Such a call is reported as an error that does not
// stop the simulation: the remaining statements run, the value returned by
// $timeunit / $timeprecision is not checked, and the exit status reports the
// error.
`timescale 1ns/1ps
module svprinttimescale4;
  int u, x;
  initial begin
    $printtimescale(nosuch.inst);
    $display("after printtimescale");
    x = $timeunit(nosuch);
    $display("after timeunit");
    x = $timeprecision(a.b.c);
    $display("after timeprecision");
    $printtimescale(u);         // A variable, not a scope
    #1 $printtimescale;
    $display("PASSED");
  end
endmodule
