// A random index into a rand array is not a reason to leave the constraint out.
class A;
  rand bit [1:0] idx;
  rand bit [7:0] data[4];
  rand bit [7:0] sel;
  constraint order { solve idx before sel; }
  constraint match { sel == data[idx]; }
  constraint range { foreach (data[i]) data[i] inside {[10:50]}; }
endclass

module svcrvoob4;
  initial begin
    A a = new;
    repeat (50) begin
      if (a.randomize() != 1) $error("randomize failed");
      if (a.sel != a.data[a.idx] || a.sel < 10 || a.sel > 50)
        $error("idx %0d sel %0d data %p", a.idx, a.sel, a.data);
    end
    $display("PASSED");
  end
endmodule
