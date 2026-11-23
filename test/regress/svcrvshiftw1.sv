// The shift amount of a constraint keeps its own width: an amount which does not fit in the width of
// the shifted value is not truncated (IEEE 1800-2017 11.4.10)
class S;
  rand bit [6:0] prot;
  rand bit [31:0] ramt;
  bit [31:0] amount;
endclass
module svcrvshiftw1;
  S s;
  int fails = 0, r;
  bit [6:0] seen_hi;
  initial begin
    s = new;
    s.amount = 32'd128;
    repeat (20) begin
      r = s.randomize() with { (s.prot >> s.amount) == '0; };
      if (!r) begin fails++; $error("amount 128 failed"); end
      if (s.prot[6]) seen_hi[6] = 1;
    end
    if (!seen_hi[6]) begin fails++; $error("prot is not free for amount 128"); end
    s.amount = 32'd4;
    repeat (20) begin
      r = s.randomize() with { (s.prot >> s.amount) == '0; };
      if (!r || s.prot >= 16) begin fails++; $error("amount 4: prot=%0d", s.prot); end
    end
    s.amount = '1;
    repeat (20) begin
      r = s.randomize() with { s.ramt == s.amount; (s.prot << s.ramt) == '0; };
      if (!r) begin fails++; $error("amount -1 failed"); end
    end
    s.amount = 0;
    repeat (20) begin
      r = s.randomize() with { (s.prot >> s.amount) == '0; };
      if (!r || s.prot != 0) begin fails++; $error("amount 0: prot=%0d", s.prot); end
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
