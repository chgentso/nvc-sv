// Array elements which are structures without rand members are state in a
// foreach constraint
typedef struct { bit [7:0] v; } sub_t;
typedef struct { sub_t subs[]; } outer_t;

class k;
  rand outer_t cfgs[];
  rand bit flip;
  constraint c {
    foreach (cfgs[i]) cfgs[i].subs[0].v[0] != flip;
  }
  function new();
    cfgs = new[3];
    foreach (cfgs[i]) begin
      cfgs[i].subs = new[2];
      cfgs[i].subs[0].v = 2 * i;
    end
  endfunction
endclass

module svcrvstructunref1;
  k x;
  int bad, ones;
  initial begin
    x = new;
    for (int n = 0; n < 30; n++) begin
      if (!x.randomize()) begin bad++; continue; end
      ones += x.flip;
      if (x.cfgs[1].subs[0].v != 2) bad++;
    end
    // Bit 0 of every value is zero so flip is one
    if (bad == 0 && ones == 30) $display("PASSED");
    else $display("FAILED %0d", bad);
  end
endmodule
