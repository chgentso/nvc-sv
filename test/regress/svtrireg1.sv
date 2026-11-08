// An undriven trireg net holds x; a driven one takes the driven value
// IEEE 1800-2017 6.6.4 (trireg: the initial value of an undriven trireg
// is x), 6.6 (other nets start at z)
module svtrireg1;
  trireg t;
  trireg [3:0] tv;
  trireg td;
  wire w;
  logic d = 1'b0;
  assign td = d;
  int errs;
  initial begin
    #1;
    if (t !== 1'bx) begin errs++; $display("FAILED t %b", t); end
    if (tv !== 4'bxxxx) begin errs++; $display("FAILED tv %b", tv); end
    if (w !== 1'bz) begin errs++; $display("FAILED w %b", w); end
    if (td !== 1'b0) begin errs++; $display("FAILED td %b", td); end
    d = 1'b1;
    #1;
    if (td !== 1'b1) begin errs++; $display("FAILED td %b", td); end
    if (errs == 0) $display("PASSED");
  end
endmodule
