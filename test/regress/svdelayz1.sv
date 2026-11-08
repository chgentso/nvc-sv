// IEEE 1800-2017 6.6: the default initialization value of a net is z and
// a net with drivers takes their output value.  10.3.3: the delay of a
// continuous assignment is the time between a change of the right-hand
// side and the assignment to the net, so a net driven only by delayed
// assignments holds z until the first update takes effect.
`timescale 1ns/1ns
module svdelayz1;
  logic [3:0] a = 4'h5;
  wire [3:0] w, w2, w4, w5;
  wire [3:0] #2 w3 = a;
  assign #2 w = a;
  assign w2 = a;
  assign #2 w4 = a;
  assign w4 = 4'bzz11;
  assign #2 {w5[3:2], w5[1:0]} = a;
  int err = 0;
  initial begin
    #1;
    if (w !== 4'bzzzz) err++;
    if (w2 !== 4'b0101) err++;
    if (w3 !== 4'bzzzz) err++;
    if (w4 !== 4'bzz11) err++;
    if (w5 !== 4'bzzzz) err++;
    #2;
    if (w !== 4'b0101) err++;
    if (w3 !== 4'b0101) err++;
    if (w4 !== 4'b01x1) err++;
    if (w5 !== 4'b0101) err++;
    if (err == 0) $display("PASSED");
    else $display("FAILED %0d", err);
  end
endmodule
