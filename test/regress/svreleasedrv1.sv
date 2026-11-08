// Expected values: IEEE 1800-2017 10.6.2 (force overrides a continuous assignment; the forced expression is
// continuously evaluated like a procedural continuous assignment, 10.6.1; release of a variable that has a continuous
// assignment reinstates that assignment; release of a variable without another driver keeps the value until the next
// assignment), 10.3 (continuous assignment), 9.4.1 (delay control)
`timescale 1ns/1ns
module svreleasedrv1;
  logic [7:0] q = 5;
  logic [7:0] d;
  logic [7:0] p;
  int err = 0;
  function automatic void chk(bit ok, string m);
    if (!ok) begin $error("%s", m); err++; end
  endfunction
  assign d = q + 8'd1;
  initial begin
    #1 chk(d === 6, "continuous assign");
    force d = q + 8'd10;
    #1 chk(d === 15, "forced d");
    q = 7;
    #1 chk(d === 17, "forced expression follows q");
    release d;
    #1 chk(d === 8, "release reinstates the continuous assignment");
    q = 9;
    #1 chk(d === 10, "continuous assignment active again");
    p = 3;
    force p = 20;
    #1 chk(p === 20, "forced p");
    release p;
    #1 chk(p === 20, "released p keeps value");
    p = 4;
    #1 chk(p === 4, "p assigned");
    if (err == 0) $display("PASSED");
  end
endmodule
