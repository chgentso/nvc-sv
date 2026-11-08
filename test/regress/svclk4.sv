// Expected values: IEEE 1800-2017 14.16 (synchronous drive with intra-assignment cycle delay
// `cb.x <= ##N v`: the RHS is sampled when the statement executes, the drive happens N
// clocking events later plus the output skew), 14.3 (default output skew #1), 25.9
// (virtual interface), 9.4.2 (value change events), 6.8 (variables start at x).
// Clock edges at 5, 15, 25, ...  Expected drives (statement time -> effective time):
//   edge 5 : data <= ##2 40 (sv changes to 41 afterwards, the 40 is kept) -> 25 + 1 = 26
//            valid <= ##1 1  -> 15 + 1 = 16
//   edge 25: valid <= ##2 0  -> 45 + 1 = 46;  bi.cb.data <= ##3 77 -> 55 + 1 = 56
// Value changes are recorded per signal (the order of same-time changes is not fixed).
`timescale 1ns/1ns
interface bus_if(input logic clk);
  logic [7:0] data;
  logic valid;
  clocking cb @(posedge clk);
    default input #1step output #1;
    output data, valid;
  endclocking
endinterface

module svclk4;
  logic clk = 0;
  initial repeat (24) #5 clk = ~clk;
  bus_if bi(clk);
  virtual bus_if vif;
  logic [7:0] sv;
  int errors = 0;
  // history entries: {value (x kept), time}
  logic [39:0] dh[$], vh[$];
  initial begin
    vif = bi;
    sv = 8'h40;
    @(vif.cb);
    vif.cb.data <= ##2 sv;
    sv = 8'h41;
    vif.cb.valid <= ##1 1;
    @(vif.cb);
    @(vif.cb);
    vif.cb.valid <= ##2 0;
    bi.cb.data <= ##3 8'h77;
    repeat (6) @(vif.cb);
    #3;
    if ($time != 88) begin $error("end time %0t, expected 88", $time); errors++; end
    if (dh.size() != 2 || dh[0] !== {8'h40, 32'd26} || dh[1] !== {8'h77, 32'd56})
      begin $error("data history %p, expected (40@26, 77@56)", dh); errors++; end
    if (vh.size() != 2 || vh[0] !== {7'b0, 1'b1, 32'd16} || vh[1] !== {7'b0, 1'b0, 32'd46})
      begin $error("valid history %p, expected (1@16, 0@46)", vh); errors++; end
    if (bi.data !== 8'h77 || bi.valid !== 1'b0)
      begin $error("final data=%h valid=%b, expected 77 0", bi.data, bi.valid); errors++; end
    if (errors == 0) $display("PASSED");
  end
  always @(bi.data) dh.push_back({bi.data, 32'($time)});
  always @(bi.valid) vh.push_back({7'b0, bi.valid, 32'($time)});
endmodule
