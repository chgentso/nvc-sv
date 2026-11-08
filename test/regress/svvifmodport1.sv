// Virtual interface clocking blocks reached through the interface and through a modport.
// Expected values: IEEE 1800-2017 14.3 (clocking event), 14.4 (input skew: sampled #1 before the
// clocking event), 25.9 (virtual interfaces), 25.5 (modport clocking).
`timescale 1ns/1ns
interface u_if;
  logic clk = 0;
  logic [3:0] d = 0;
  clocking cb @(posedge clk);
    input #1 d;
  endclocking
  modport mp(clocking cb);
endinterface
class mon;
  virtual u_if vif;
  int errors;
  task run();
    // d is incremented at t=4,8,12,16,20; the clock rises at t=5,15,25.
    @(vif.cb);
    if (vif.d !== 4'd1) begin $error("a: vif.d=%0d exp 1", vif.d); errors++; end
    @(vif.mp.cb);
    if (vif.cb.d !== 4'd3) begin $error("b: vif.cb.d=%0d exp 3", vif.cb.d); errors++; end
    @(vif.mp.cb);
    if (vif.mp.cb.d !== 4'd5) begin $error("c: vif.mp.cb.d=%0d exp 5", vif.mp.cb.d); errors++; end
  endtask
endclass
module svvifmodport1;
  u_if i();
  mon m = new;
  always #5 i.clk = ~i.clk;
  initial begin
    m.vif = i; m.run();
    if (m.errors == 0) $display("PASSED");
    $finish;
  end
  initial begin repeat (5) begin #4 i.d++; end end
endmodule
