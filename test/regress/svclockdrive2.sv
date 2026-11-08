// Expected values: IEEE 1800-2017 14.4 / 14.16 (output clockvar drives: default output skew is
// #0; a drive executed off-event takes effect at the next clocking event, a drive executed at a
// clocking event takes effect at that event), 14.3 (input default skew #1step), 25.9 (virtual
// interface used from a class), 9.4.2 (value change events).
// Clock edges at 5, 15, 25, ...  main drives o <= 1 at time 2 (-> takes effect at edge 5).
// drv, woken at edge 5, drives o <= 1 (same value) and at edge 15 drives o <= 0.
// Same-time ordering of the processes woken by one clocking event is not fixed (4.7), so
// the values are read 1 time unit after the event and wake-up times are checked separately.
`timescale 1ns/1ns
interface ifc(input bit clk);
  logic d, o;
  clocking cb @(posedge clk);
    input d;
    output o;
  endclocking
endinterface
class c;
  virtual ifc vif;
  time t_woke, t_after, t_second;
  task drv();
    @(vif.cb);
    t_woke = $time;
    vif.cb.o <= 1'b1;
    @(vif.cb);
    t_after = $time;
    vif.cb.o <= 1'b0;
    @(vif.cb);
    t_second = $time;
  endtask
endclass
module svclockdrive2;
  bit clk;
  ifc i(clk);
  c o = new;
  int errors = 0;
  time t_m1, t_m2;
  logic [39:0] oh[$];      // history of i.o: {value, time}
  initial forever #5 clk = ~clk;
  initial begin
    o.vif = i;
    fork o.drv(); join_none
    #2 i.cb.o <= 1;
    if (i.o !== 1'bx) begin $error("o = %b before the first edge, expected x", i.o); errors++; end
    @(i.cb);
    t_m1 = $time;
    #1;
    if (i.o !== 1'b1) begin $error("o = %b at 6, expected 1", i.o); errors++; end
    @(i.cb);
    t_m2 = $time;
    #1;
    if (i.o !== 1'b0) begin $error("o = %b at 16, expected 0 (driven by drv at 15)", i.o); errors++; end
    #29;   // 45
    if (t_m1 !== 5 || t_m2 !== 15) begin $error("main woke at %0t and %0t, expected 5 and 15", t_m1, t_m2); errors++; end
    if (o.t_woke !== 5 || o.t_after !== 15 || o.t_second !== 25)
      begin $error("drv woke at %0t, %0t, %0t, expected 5, 15, 25", o.t_woke, o.t_after, o.t_second); errors++; end
    if (oh.size() != 2 || oh[0] !== {8'h01, 32'd5} || oh[1] !== {8'h00, 32'd15})
      begin $error("o history %p, expected (1@5, 0@15)", oh); errors++; end
    if (errors == 0) $display("PASSED");
    $finish;
  end
  always @(i.o) oh.push_back({7'b0, i.o, 32'($time)});
endmodule
