`timescale 1ps/1ps
// A non-blocking assignment to a variable which has been released by
// deassign (IEEE 1800-2017 10.6.1), with a delayed continuous assignment
// of the variable to a net as in a flip-flop model
module svnbadeassign1;
  reg Q_out, gs, C = 0;
  wire Q;
  assign #100 Q = Q_out;
  always @(gs) if (gs) assign Q_out = 0; else deassign Q_out;
  always @(posedge C) Q_out <= 1;
  initial begin
    gs = 1;
    #100 gs = 0;
    #100 C = 1;
    #1 if (Q_out !== 1'b1) $display("FAILED early %b", Q_out);
    #500;
    if (Q === 1'b1 && Q_out === 1'b1) $display("PASSED");
    else $display("FAILED Q=%b Q_out=%b", Q, Q_out);
  end
endmodule
