`timescale 1ns/1ns
// Concurrent assertions on signals reached through an interface port and through
// an interface instance, with failure counted by the action blocks.
// Expected values: IEEE 1800-2017 16.5.1 / 16.9.3 (assertion expressions use
// values sampled in the preponed region of the clock tick), 16.12.7 (|->),
// 16.9.3 ($rose, $past), 25.3 (interface ports), 16.14.1 (fail action).
//
// clk has posedges at 5, 15, 25, ...  bus.req = 1 from 12 to 22; gnt_r takes req
// at each posedge (nonblocking), so gnt is 1 only after the posedge at 15.
//   a1: req |-> ##1 gnt     req sampled 1 at 15, gnt sampled 1 at 25: no failure
//   a2: $rose(gnt) |-> $past(req)  gnt rises at the tick 25, req at 15 was 1: none
//   a3: req |-> gnt         req = 1 and gnt (sampled) = 0 at the tick 15: one failure at 15
// w.cyc and w.stb are set by nonblocking assignments at 45 and cleared at 55, so
//   a_hold: (cyc && stb) |=> (cyc && stb)   true sampled at 55, false at 65: one failure at 65
package svapiface1_pkg;
  int fails[4];
  longint ftime[4];
  function automatic void fail(int id, longint t);
    fails[id]++;
    ftime[id] += t;
  endfunction
endpackage

interface ifc_t (input logic clk);
  logic req, gnt;
endinterface

module dut(ifc_t i);
  import svapiface1_pkg::*;
  logic gnt_r = 0;
  assign i.gnt = gnt_r;
  always_ff @(posedge i.clk) gnt_r <= i.req;
  a1: assert property (@(posedge i.clk) i.req |-> ##1 i.gnt)
    else fail(0, $time);
  a2: assert property (@(posedge i.clk) $rose(i.gnt) |-> $past(i.req))
    else fail(1, $time);
  a3: assert property (@(posedge i.clk) i.req |-> i.gnt)
    else fail(2, $time);
endmodule

interface wi (input logic clk);
  logic cyc, stb;
endinterface

module svapiface1;
  import svapiface1_pkg::*;
  logic clk = 0;
  int errors = 0;
  always #5 clk = ~clk;
  ifc_t bus(clk);
  dut d(bus);
  wi w (clk);
  a_hold: assert property (@(posedge clk) (w.cyc && w.stb) |=> (w.cyc && w.stb))
    else fail(3, $time);
  initial begin
    bus.req = 0;
    #12 bus.req = 1; #10 bus.req = 0;
    #60;
    if ($time !== 82) begin $error("first process ends at %0t, expected 82", $time); errors++; end
  end
  initial begin
    w.cyc = 0; w.stb = 0;
    repeat (5) @(posedge w.clk);
    w.cyc <= 1; w.stb <= 1;
    @(posedge w.clk);
    w.cyc <= 0; w.stb <= 0;
    repeat (5) @(posedge w.clk);
    if ($time !== 105) begin $error("second process ends at %0t, expected 105", $time); errors++; end
    if (fails[0] !== 0) begin $error("a1 failed %0d times, expected 0", fails[0]); errors++; end
    if (fails[1] !== 0) begin $error("a2 failed %0d times, expected 0", fails[1]); errors++; end
    if (fails[2] !== 1 || ftime[2] !== 15) begin $error("a3 failed %0d times (time sum %0d), expected once at 15", fails[2], ftime[2]); errors++; end
    if (fails[3] !== 1 || ftime[3] !== 65) begin $error("a_hold failed %0d times (time sum %0d), expected once at 65", fails[3], ftime[3]); errors++; end
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
