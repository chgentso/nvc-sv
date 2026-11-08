`timescale 1ns/1ns
// Static and automatic tasks and functions with output/inout formals, nonblocking assignments to the formals
// Expected values: IEEE 1800-2017 13.5 (copy-in/copy-out of output and inout arguments at the call and at the
// return; a nonblocking assignment still pending at the return only updates the formal afterwards), 13.4, 13.3
module svnbastatic1;
  bit clk = 0; always #5 clk = !clk;
  task tk(input int v, output int o, inout int i);
    @(posedge clk);
    i <= v + 1;
    o <= v;
  endtask
  task automatic ta(inout int i); i = 1; endtask
  function void fn(inout int i); i = i + 1; endfunction
  int oo, ii;
  int fails = 0;
  initial begin
    // The nonblocking assignments to i and o are pending when the task returns at the first clock edge, so the
    // copy-out takes the old values of the formals: o = 0 (initial value), i = 0 (copied in from ii)
    tk(3, oo, ii);
    #1 if (oo !== 0 || ii !== 0) begin $error("tk: oo=%0d ii=%0d", oo, ii); fails++; end
    ta(ii); fn(ii);
    if (ii !== 2) begin $error("ta/fn: ii=%0d", ii); fails++; end
    #20;
    if (fails == 0) $display("PASSED");
    $finish;
  end
endmodule
