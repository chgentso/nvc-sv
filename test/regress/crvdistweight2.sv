// A dist whose weights are rand variables is a warning: the weights are
// ignored and the argument only has to be in one of the items
class item;
  rand int unsigned w0;
  rand int unsigned w1;
  rand bit [3:0] v;
  rand bit [3:0] u;
  constraint wc { w0 inside {[1:10]}; w1 inside {[1:10]}; }
  constraint dc { v dist { 3 := w0, [8:9] :/ w1 }; }
  constraint fixed { u dist { 1 := 2, 4 := 5 }; }
endclass

module crvdistweight2;
  initial begin
    item it = new;
    for (int t = 0; t < 20; t++) begin
      if (!it.randomize()) $fatal(1, "randomize failed");
      if (it.v != 3 && it.v != 8 && it.v != 9) $fatal(1, "v %0d", it.v);
      if (it.u != 1 && it.u != 4) $fatal(1, "u %0d", it.u);
    end
    $display("PASSED");
  end
endmodule
