// Expected values: IEEE 1800-2017 25.7 (tasks in interfaces see the interface's variables and nets), 21.2 (system
// tasks / $sformatf in such a task), 6.5/10.3 (net driven by a continuous assignment from the enclosing module),
// 11.6.1 (sum of a 1-bit and a 4-bit value is evaluated in 32 bits when assigned to an int).
`timescale 1ns/1ns
interface svifacetask1_if(input clk);
  logic q = 0;
  logic [3:0] r = 4'h3;
  wire p;
  typedef struct { time t; string txt; int sum; } snap_t;
  snap_t snaps[$];
  task show;
    snap_t s;
    s.t = $time;
    s.txt = $sformatf("q=%b r=%h p=%b", q, r, p);
    s.sum = q + r;
    snaps.push_back(s);
  endtask
  initial begin #1 show(); end
endinterface

module svifacetask1;
  logic clk = 0;
  logic drv = 0;
  svifacetask1_if ctrl(clk);
  assign ctrl.p = drv;
  int err = 0;
  task automatic chk(int i, time t, string txt, int sum);
    if (ctrl.snaps[i].t != t || ctrl.snaps[i].txt != txt || ctrl.snaps[i].sum != sum) begin
      err++; $error("snap %0d: t=%0t '%s' sum=%0d", i, ctrl.snaps[i].t, ctrl.snaps[i].txt, ctrl.snaps[i].sum);
    end
  endtask
  initial begin
    #5 ctrl.show();
    ctrl.q = 1; ctrl.r = 4'h9; drv = 1;
    #1 ctrl.show();
    if (ctrl.snaps.size() != 3) begin err++; $error("expected 3 snapshots, got %0d", ctrl.snaps.size()); end
    else begin
      chk(0, 1, "q=0 r=3 p=0", 3);
      chk(1, 5, "q=0 r=3 p=0", 3);
      chk(2, 6, "q=1 r=9 p=1", 10);
    end
    if (err == 0) $display("PASSED");
  end
endmodule
