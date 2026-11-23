// foreach over a row of a two dimensional array selected by an enclosing
// foreach, and a dist for each element of an array sized by the solver
class A;
  rand bit [2:0] m[2][3];
  rand bit [2:0] d[];
  constraint c {
    foreach (m[i]) foreach (m[i][j]) m[i][j] inside {[1+i:3+i]};
    d.size() == 4;
    foreach (d[i]) d[i] dist { [0:1] :/ 90, [2:5] :/ 10 };
  }
endclass

module crvforeachrow1;
  initial begin
    automatic A a = new;
    int low = 0, high = 0;
    for (int t = 0; t < 50; t++) begin
      if (!a.randomize()) $fatal(1, "randomize failed");
      foreach (a.m[i, j])
        if (a.m[i][j] < 1 + i || a.m[i][j] > 3 + i)
          $fatal(1, "m[%0d][%0d] = %0d", i, j, a.m[i][j]);
      if (a.d.size() != 4) $fatal(1, "size %0d", a.d.size());
      foreach (a.d[i]) begin
        if (a.d[i] > 5) $fatal(1, "d[%0d] = %0d", i, a.d[i]);
        if (a.d[i] <= 1) low++; else high++;
      end
    end
    if (low <= high || high == 0) $fatal(1, "weights ignored %0d %0d", low, high);
    $display("PASSED");
  end
endmodule
