// A dist in which every weight is zero does not constrain the variable, also when the weights are
// not known until the call (IEEE 1800-2017 18.5.4)
class Z;
  rand bit [7:0] x;
  rand bit [7:0] y;
  int w1, w2;
  constraint c1 { x dist { 8'd0 := 0, 8'd1 := 0, 8'd2 := 0 }; }
  constraint c2 { y dist { 8'd5 := w1, 8'd6 := w2 }; }
endclass
module svcrvdistzero1;
  Z z;
  int fails = 0, seen_other = 0, seen6 = 0;
  initial begin
    z = new;
    repeat (200) begin
      if (!z.randomize()) begin fails++; $error("randomize failed with zero weights"); end
      if (z.x > 2 || z.y > 6) seen_other++;
    end
    if (seen_other == 0) begin fails++; $error("not unconstrained"); end
    z.w1 = 1; z.w2 = 3;
    repeat (200) begin
      if (!z.randomize() || (z.y != 5 && z.y != 6)) begin fails++; $error("y=%0d", z.y); end
      if (z.y == 6) seen6++;
    end
    if (seen6 < 100) begin fails++; $error("weights not used: seen6=%0d", seen6); end
    if (fails == 0) $display("PASSED");
  end
endmodule
