class item;
  rand bit [7:0] v;
  constraint c { v inside {[10:20]}; }
endclass
class holder;
  rand item m[string];
  rand item q[int];
  rand bit [3:0] k;
  function new();
    m["a"] = new; m["b"] = new;
    q[3] = new; q[9] = new; q[11] = new; q[20] = null;
  endfunction
endclass
module svcrvassocobj1;
  holder h; int bad;
  initial begin
    h = new;
    repeat (10) begin
      if (!h.randomize()) bad++;
      foreach (h.m[i]) if (h.m[i].v < 10 || h.m[i].v > 20) bad++;
      foreach (h.q[i]) if (h.q[i] != null && (h.q[i].v < 10 || h.q[i].v > 20)) bad++;
    end
    if (h.m["a"].v == h.m["b"].v && h.q[3].v == h.q[9].v && h.q[9].v == h.q[11].v) bad++;
    if (bad == 0) $display("PASSED"); else $display("FAILED %0d", bad);
  end
endmodule
