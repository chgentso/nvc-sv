// Synchronous drives of clockvars whose signal is a net: directly, through a
// virtual interface, through a modport which exports the clocking block, with
// a cycle delay and with output skews #0, #2 and none; two drives in a cycle
// (the last one wins) and a drive of 'z releasing the net.
// Expected values: IEEE 1800-2017 14.3 (clocking block signals can be nets or
// variables), 14.16 (synchronous drives).
`timescale 1ns/1ns
interface u_if(input logic clk);
  wire a;
  wire [3:0] b;
  wire c;
  logic v;
  clocking cb @(posedge clk);
    output a;
    output #2 b;
    output #0 c;
    output v;
  endclocking
  modport mp(clocking cb);
endinterface

class drv;
  virtual u_if vif;
  virtual u_if.mp mvif;
  task run();
    vif.cb.a <= 1'b1;            // takes effect at the next edge (5)
    vif.cb.b <= 4'd9;            // 5 + 2
    vif.cb.v <= 1'b1;
    @(vif.cb);                   // 5
    vif.cb.a <= 1'b0;            // 5: applied now
    vif.cb.a <= 1'b1;            // last wins
    mvif.cb.c <= 1'b1;
    #3;                          // 8
    mvif.cb.b <= 4'd3;           // applied at 15 + 2
    @(vif.cb);                   // 15
    vif.cb.a <= 1'bz;
    vif.cb.c <= ##1 1'b0;        // applied at 25
    vif.cb.v <= 1'b0;
  endtask
endclass

module svclockdrive4;
  logic clk = 0;
  always #5 clk = ~clk;
  u_if i(clk);
  drv d = new;
  int errors = 0;
  wire other = i.a;

  task check(string what, logic [7:0] got, logic [7:0] exp);
    if (got !== exp) begin
      $error("%s: got %b expected %b at %t", what, got, exp, $time);
      errors++;
    end
  endtask

  initial begin
    d.vif = i;
    d.mvif = i;
    check("a init", i.a, 1'bz);
    d.run();
  end

  initial begin
    #4;   check("a@4", i.a, 1'bz);
    #2;   check("a@6", i.a, 1'b1); check("b@6", i.b, 4'bzzzz);
          check("c@6", i.c, 1'b1); check("v@6", i.v, 1'b1);
          check("other@6", other, 1'b1);
    #2;   check("b@8", i.b, 4'd9);
    #8;   check("a@16", i.a, 1'bz); check("b@16", i.b, 4'd9);
          check("v@16", i.v, 1'b0);
    #2;   check("b@18", i.b, 4'd3); check("c@18", i.c, 1'b1);
    #8;   check("c@26", i.c, 1'b0);
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
