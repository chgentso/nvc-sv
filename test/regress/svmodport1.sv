`timescale 1ns/1ns
// Modport expressions and the members visible through a modport
// Expected values: IEEE 1800-2017 25.5 (modports and modport expressions: a named expression is read through
// the modport and follows the current value of the interface member; an output expression can be written),
// 9.4.2 (event control on a modport expression)
interface svmodport1_if (input logic clk);
  logic [7:0] data;
  logic en, rdy;
  logic [3:0] nib;
  modport mp (input clk, input .lo(data[3:0]), input .hi(data[7:4]), input .busy(en & ~rdy), input .both(en & rdy),
              input .whole(data), output .ctl(en));
  modport sl (input en);
endinterface
module svmodport1_cons(svmodport1_if.mp p);
  int fails = 0;
  logic [3:0] chg_val[$];
  time chg_time[$];
  initial begin
    #2;
    if (p.lo !== 4'h5 || p.hi !== 4'ha || p.busy !== 1'b1 || p.both !== 1'b0 || p.whole !== 8'ha5) begin
      $error("t=2: lo=%h hi=%h busy=%b both=%b whole=%h", p.lo, p.hi, p.busy, p.both, p.whole); fails++;
    end
    p.ctl = 0;
    #1 if (p.busy !== 1'b0) begin $error("t=3: busy=%b after ctl=0", p.busy); fails++; end
    #3;  // t=6
    // the change of data at t=5 is the only change after time 0 (the time-0 initialisation may or may not be seen)
    begin
      automatic int n = 0;
      foreach (chg_time[i]) if (chg_time[i] > 0) begin
        n++;
        if (chg_time[i] != 5 || chg_val[i] !== 4'hc) begin $error("lo changed to %h at %0t", chg_val[i], chg_time[i]); fails++; end
      end
      if (n != 1) begin $error("expected one change of lo after time 0, got %0d", n); fails++; end
    end
    if (fails == 0) $display("PASSED");
  end
  always @(p.lo) begin chg_val.push_back(p.lo); chg_time.push_back($time); end
endmodule
module svmodport1_ro(svmodport1_if.sl q);
  initial begin
    #1 if (q.en !== 1'b1) $error("ro: en=%b", q.en);
  end
endmodule

module svmodport1;
  logic clk = 0;
  svmodport1_if i(clk);
  svmodport1_cons c(i);
  svmodport1_ro r(i);
  initial begin i.data = 8'hA5; i.en = 1; i.rdy = 0; #5 i.data = 8'h3C; end
endmodule
