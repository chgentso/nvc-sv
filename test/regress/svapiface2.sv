`timescale 1ns/1ns
// Concurrent assertions on a parameterised interface reached through a modport
// port, with failure counted by the action blocks.
// Expected values: IEEE 1800-2017 16.5.1 / 16.9.3 (sampled values), 16.12.7 (|->),
// 16.9.3 ($rose, $past), 16.15 (disable iff), 25.5 (modport), 25.3 (parameterised
// interface), 16.14.1 (fail action).
//
// clk has posedges at 5, 15, 25, ...  req = 01 from 12 to 22 and 10 from 32 to 42.
// gnt_r takes req at each posedge (nonblocking), so gnt = 01 only between the
// ticks 15 and 25 and gnt = 10 between 35 and 45.
//   a1: req != 0 |-> ##1 gnt != 0      req sampled nonzero at 15 and 35; gnt nonzero
//                                      at 25 and 45: no failure
//   a2: $rose(gnt[0]) |-> $past(req[0])  gnt[0] rises at the tick 25, req[0] at 15
//                                      was 1: no failure (rst is never set)
//   a3: req[1] |-> gnt[1]              req[1] = 1 at 35, gnt[1] sampled 0: one failure at 35
package svapiface2_pkg;
  int fails[3];
  longint ftime[3];
  function automatic void fail(int id, longint t);
    fails[id]++;
    ftime[id] += t;
  endfunction
endpackage

interface ifc_t #(parameter int N = 2) (input logic clk, input logic rst);
  logic [N-1:0] req, gnt;
  logic busy;
  modport arb (input clk, rst, req, output gnt, busy);
endinterface
module dut #(parameter int N = 2) (ifc_t.arb i);
  import svapiface2_pkg::*;
  logic [N-1:0] gnt_r = 0;
  assign i.gnt = gnt_r;
  always_ff @(posedge i.clk) gnt_r <= i.req;
  a1: assert property (@(posedge i.clk) i.req != '0 |-> ##1 i.gnt != '0) else fail(0, $time);
  a2: assert property (@(posedge i.clk) disable iff (i.rst) $rose(i.gnt[0]) |-> $past(i.req[0])) else fail(1, $time);
  a3: assert property (@(posedge i.clk) i.req[1] |-> i.gnt[1]) else fail(2, $time);
endmodule
module svapiface2;
  import svapiface2_pkg::*;
  logic clk = 0, rst = 0;
  int errors = 0;
  always #5 clk = ~clk;
  ifc_t #(2) bus(clk, rst);
  dut #(2) d(bus);
  initial begin
    bus.req = 0;
    #12 bus.req = 2'b01; #10 bus.req = 0; #10 bus.req = 2'b10; #10 bus.req = 0;
    #50;
    if ($time !== 92) begin $error("ends at %0t, expected 92", $time); errors++; end
    if (fails[0] !== 0) begin $error("a1 failed %0d times, expected 0", fails[0]); errors++; end
    if (fails[1] !== 0) begin $error("a2 failed %0d times, expected 0", fails[1]); errors++; end
    if (fails[2] !== 1 || ftime[2] !== 35) begin $error("a3 failed %0d times (time sum %0d), expected once at 35", fails[2], ftime[2]); errors++; end
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
