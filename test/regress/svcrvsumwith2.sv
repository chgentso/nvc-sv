// Array reduction methods with a with clause that is not just the element, over a dynamic array that
// has the size which the constraints ask for (IEEE 1800-2017 7.12.3, 18.5.8): the with expression is
// evaluated for each element chosen by the solver.
class A;
  rand bit [7:0] d[];
  function new; d = new[4]; endfunction
  constraint c1 { d.size() == 4; d.sum() with (int'(item) * 2) == 300; }
  constraint c2 { d.sum() with (item.index) == 6; }
  constraint c3 { d.xor() with (item.index) == 0; d.product() with (item.index + 1) == 24; }
  constraint c4 { d.or() with (int'(item)) < 256; }
  constraint c5 { foreach (d[i]) d[i] > 10; }
endclass
module svcrvsumwith2;
  A a;
  int fails = 0, t;
  initial begin
    a = new;
    repeat (20) begin
      if (!a.randomize()) begin fails++; $error("randomize failed"); end
      else begin
        t = 0;
        foreach (a.d[i]) begin t += int'(a.d[i]) * 2; end
        if (a.d.size() != 4 || t != 300) begin
          fails++; $error("size %0d sum %0d", a.d.size(), t);
        end
      end
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
