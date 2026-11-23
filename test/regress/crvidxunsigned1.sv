// A narrow unsigned index of a rand array in a constraint is not negative
class C;
  rand bit [1:0] idx;
  rand bit [7:0] a[4];
  rand bit [7:0] b[4];
  constraint c { a[idx] == 8'd10; foreach (b[i]) b[i] == a[idx] + i; }
endclass

module crvidxunsigned1;
  initial begin
    automatic C c = new;
    c.idx.rand_mode(0);
    for (int k = 0; k < 4; k++) begin
      c.idx = k;
      if (!c.randomize()) $fatal(1, "randomize failed for idx %0d", k);
      if (c.a[k] != 10) $fatal(1, "a[%0d] = %0d", k, c.a[k]);
      foreach (c.b[i])
        if (c.b[i] != 10 + i) $fatal(1, "b[%0d] = %0d", i, c.b[i]);
    end
    $display("PASSED");
  end
endmodule
