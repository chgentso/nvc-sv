// A dist on a variable which is not randomized is a membership test (IEEE 1800-2017 18.5.3): the values with
// a weight of zero are not in the set.
class Mixed;
  rand int unsigned d;
  int unsigned nf;
  constraint c { d inside {[1:9]}; nf dist { 5 := 9, [70:80] :/ 1, 99 := 0 }; }
endclass
module svcrvdiststate1;
  Mixed m = new;
  int ok;
  initial begin
    m.nf = 5;  ok = m.randomize(); if (ok != 1) $error("5 %0d", ok);
    m.nf = 75; ok = m.randomize(); if (ok != 1) $error("75 %0d", ok);
    m.nf = 7;  ok = m.randomize(); if (ok != 0) $error("7 %0d", ok);
    m.nf = 99; ok = m.randomize(); if (ok != 0) $error("99 weight 0 %0d", ok);
    $display("PASSED");
  end
endmodule
