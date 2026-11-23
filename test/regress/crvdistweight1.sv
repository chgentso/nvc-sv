// dist weights which are rand variables are not read before the solve
class item;
  rand int unsigned arr[4];
  rand int unsigned w0;
  rand int unsigned w1;
  rand bit [3:0] v;
  constraint wc { w0 inside {[1:10]}; w1 inside {[1:10]}; }
  constraint dc {
    foreach (arr[i]) arr[i] dist { 0 :/ w0, [1:15] :/ w1 };
    v dist { 3 := w0, [8:9] :/ w1 };
  }
endclass

module crvdistweight1;
  initial begin
    item it = new;
    int seen3 = 0, seen8 = 0;
    for (int t = 0; t < 40; t++) begin
      if (!it.randomize()) $fatal(1, "randomize failed on call %0d", t);
      foreach (it.arr[i])
        if (it.arr[i] > 15) $fatal(1, "arr out of range: %0d", it.arr[i]);
      if (it.v != 3 && it.v != 8 && it.v != 9)
        $fatal(1, "v out of range: %0d", it.v);
      if (it.v == 3) seen3++;
      else seen8++;
    end
    if (seen3 == 0 || seen8 == 0) $fatal(1, "no choice %0d %0d", seen3, seen8);
    $display("PASSED");
  end
endmodule
