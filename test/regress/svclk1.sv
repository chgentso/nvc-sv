// Expected values: IEEE 1800-2017 14.3-14.4 (clocking blocks, input skew #1step / #3 sampled that
// long before the clocking event), 14.4 (output skew: synchronous drive at the event plus skew;
// a drive issued off-event waits for the next event), 14.11 (##N cycle delay on the default
// clocking), 14.7 (modport with a clocking block), 25.9 (virtual interface), 4.7.
// Clock edge k is at time 10k-5 (5, 15, 25, ...). cnt is incremented by an NBA at every edge, so
// the value sampled by an input #1step clockvar at edge k is k-1.
// Everything that reads a clockvar or an output-driven signal does so 1 time unit after the
// clocking event, so the checks do not depend on the order of same-time activities.
`timescale 1ns/1ns
interface bus_if(input logic clk);
  logic [7:0] data;
  logic valid;
  logic ready;
  logic [3:0] cnt;
  clocking cb @(posedge clk);
    default input #1step output #2;
    input ready, cnt;
    output data, valid;
  endclocking
  clocking mon_cb @(posedge clk);
    input data, valid, ready;
  endclocking
  clocking skew_cb @(posedge clk);
    input #3 cnt;
    output #0 ready;
  endclocking
  modport drv(clocking cb);
  initial begin data = 0; valid = 0; ready = 0; cnt = 0; end
  always @(posedge clk) cnt <= cnt + 1;
endinterface

module dut(bus_if.drv port);
  int seen_cnt[$];
  int seen_ready[$];
  int seen_time[$];
  initial begin
    forever begin
      @(port.cb);
      #1;
      seen_time.push_back($time - 1);
      seen_cnt.push_back(port.cb.cnt);
      seen_ready.push_back(port.cb.ready);
    end
  end
endmodule

module svclk1;
  logic clk = 0;
  always #5 clk = ~clk;
  bus_if bi(clk);
  dut d(bi);
  virtual bus_if vif;
  default clocking dc @(posedge clk); endclocking
  int errors = 0;

  task automatic drive(input [7:0] v);
    @(vif.cb);
    vif.cb.data <= v;
    vif.cb.valid <= 1;
    @(vif.cb);
    vif.cb.valid <= 0;
  endtask

  initial begin
    vif = bi;
    @(vif.cb);                                   // edge 5
    #1;
    if ($time != 6) begin $error("first @(cb) returned late"); errors++; end
    if (vif.cb.cnt !== 4'd0) begin $error("start cnt = %0d, expected 0", vif.cb.cnt); errors++; end
    drive(8'h11);                                // drives at 15 (data, valid) and 25 (valid), each +2
    #1;                                          // 26: data/valid driven at 17, valid=0 pending for 27
    if ($time != 26) begin $error("time %0t, expected 26", $time); errors++; end
    if (bi.data !== 8'h11 || bi.valid !== 1'b1)
      begin $error("t=26 data=%h valid=%b, expected 11 1", bi.data, bi.valid); errors++; end
    #2;                                          // 28
    if (bi.data !== 8'h11 || bi.valid !== 1'b0)
      begin $error("t=28 data=%h valid=%b, expected 11 0", bi.data, bi.valid); errors++; end
    ##2;                                         // from 28: edges 35 and 45
    #1;                                          // 46
    if ($time != 46) begin $error("##2 ended at %0t, expected 45 (+1)", $time); errors++; end
    if (bi.mon_cb.data !== 8'h11 || bi.mon_cb.valid !== 1'b0)
      begin $error("t=46 mon data=%h valid=%b, expected 11 0", bi.mon_cb.data, bi.mon_cb.valid); errors++; end
    // skew_cb.cnt: input #3 sampled at 42 for edge 45, cnt = 4 (after the edge at 35)
    if (bi.skew_cb.cnt !== 4'd4) begin $error("skew_cb.cnt = %0d, expected 4", bi.skew_cb.cnt); errors++; end
    bi.skew_cb.ready <= 1;                       // off-event drive: applied at the next edge, 55, skew 0
    @(bi.skew_cb);                               // 55
    #1;                                          // 56
    if ($time != 56) begin $error("skew_cb event at %0t, expected 55 (+1)", $time); errors++; end
    if (bi.ready !== 1'b1) begin $error("ready = %b at 56, expected 1", bi.ready); errors++; end
    ##1;                                         // from 56: edge 65
    #1;                                          // 66
    if ($time != 66) begin $error("##1 ended at %0t, expected 65 (+1)", $time); errors++; end
    // sample before edge 65 is 6, the live value is 7 after the NBA of edge 65
    if (bi.cb.cnt !== 4'd6) begin $error("cb.cnt = %0d at 66, expected 6", bi.cb.cnt); errors++; end
    if (bi.cnt !== 4'd7) begin $error("cnt = %0d at 66, expected 7", bi.cnt); errors++; end
    #24;                                         // 90: edges up to 85 have been recorded
    // dut: at edge k (time 10k-5) cb.cnt = k-1; ready is 1 from the edge at 65 on
    if (d.seen_cnt.size() != 9) begin $error("dut saw %0d clocking events, expected 9", d.seen_cnt.size()); errors++; end
    else for (int k = 1; k <= 9; k++) begin
      if (d.seen_time[k-1] != 10*k - 5) begin $error("dut event %0d at %0d", k, d.seen_time[k-1]); errors++; end
      if (d.seen_cnt[k-1] !== k - 1) begin $error("dut event %0d: cnt %0d, expected %0d", k, d.seen_cnt[k-1], k-1); errors++; end
      if (d.seen_ready[k-1] !== ((10*k - 5 >= 65) ? 1 : 0)) begin $error("dut event %0d: ready %0d", k, d.seen_ready[k-1]); errors++; end
    end
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
