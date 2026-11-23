// A foreach constraint over an associative array of objects, which names an
// object with the key and a nested array of objects through it
class leaf;
  rand bit [3:0] v;
endclass

class mid;
  rand leaf one;
  rand leaf subs[int];
  function new();
    one = new;
    subs[2] = new;
    subs[5] = new;
  endfunction
endclass

class top;
  rand mid m[string];
  constraint c {
    foreach (m[k]) {
      m[k].one.v < 3;
      foreach (m[k].subs[j]) m[k].subs[j].v == j;
    }
  }
  function new();
    m["x"] = new;
    m["y"] = new;
  endfunction
endclass

module svcrvassocobj2;
  top t;
  int bad;
  initial begin
    t = new;
    repeat (10) begin
      if (!t.randomize()) bad++;
      foreach (t.m[k]) begin
        if (t.m[k].one.v >= 3) bad++;
        foreach (t.m[k].subs[j]) if (t.m[k].subs[j].v != j) bad++;
      end
    end
    if (bad == 0) $display("PASSED");
    else $display("FAILED %0d", bad);
  end
endmodule
