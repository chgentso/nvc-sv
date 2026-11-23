// Elements of a rand queue and dynamic array of values wider than 64 bits
// are solved: constant and foreach index, the size, the upper chunk
class wd;
  rand bit [65:0] q[$];
  rand bit [95:0] d[];
  rand bit [127:0] free_q[$];
  rand int n;
  function new;
    q = '{3{0}};
    d = new[2];
  endfunction
  constraint c {
    q[0] == 0;
    q[1] == 66'h2_0000_0000_0000_0001;
    q[2] > 66'h3_0000_0000_0000_0000;
    n inside {[2:5]};
    d.size() == n;
    foreach (d[i]) d[i] == 96'h1_0000_0000_0000_0000 + i;
    free_q.size() == 3;
  }
endclass
module svcrvwidedyn1;
  int errs = 0;
  wd f = new;
  initial begin
    repeat (20) begin
      if (!f.randomize()) begin $error("failed"); errs++; end
      else begin
        if (f.q[0] != 0 || f.q[1] != 66'h2_0000_0000_0000_0001
            || f.q[2] <= 66'h3_0000_0000_0000_0000) begin
          $error("queue %h %h %h", f.q[0], f.q[1], f.q[2]); errs++;
        end
        if (f.d.size() != f.n || f.n < 2 || f.n > 5) begin
          $error("size %0d %0d", f.d.size(), f.n); errs++;
        end
        foreach (f.d[i])
          if (f.d[i] != 96'h1_0000_0000_0000_0000 + i) begin
            $error("d[%0d]=%h", i, f.d[i]); errs++;
          end
        if (f.free_q.size() != 3) begin $error("free size"); errs++; end
      end
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
