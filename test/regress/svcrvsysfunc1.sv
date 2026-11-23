// $countbits, $clog2 of a random variable and $isunbounded in a constraint (IEEE 1800-2017 20.9, 20.8.1)
class A;
  rand bit [7:0] ones3, zeros6, w, lg, any;
  rand bit [3:0] both;
  constraint c1 { $countbits(ones3, '1) == 3; }
  constraint c2 { $countbits(zeros6, '0) == 6; }
  constraint c3 { w > 0; lg == 8'($clog2(w)); lg >= 5; }
  constraint c4 { !$isunbounded(any); $countbits(both, '0, '1) == 4; $countbits(both, 'x) == 0; }
endclass
module svcrvsysfunc1;
  A a;
  int fails = 0;
  initial begin
    a = new;
    repeat (50) begin
      if (!a.randomize()) begin fails++; $error("randomize failed"); end
      else begin
        if ($countones(a.ones3) != 3) begin fails++; $error("ones3=%b", a.ones3); end
        if ($countones(a.zeros6) != 2) begin fails++; $error("zeros6=%b", a.zeros6); end
        if (a.lg != 8'($clog2(a.w)) || a.lg < 5) begin fails++; $error("w=%0d lg=%0d", a.w, a.lg); end
      end
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
