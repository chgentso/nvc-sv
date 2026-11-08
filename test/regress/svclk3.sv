// Expected values: IEEE 1800-2017 14.16 (synchronous drives: a drive issued at a clocking event
// takes effect output-skew after the event; `cb.x <= ##N v` delays the drive by N clocking
// events; the value is the RHS evaluated when the statement executes), 14.3 (clocking block
// with sequence declaration, clockvar with separate input and output skews), 14.11.
// Clock edges at 5, 15, ..., 95.  d and v have no explicit skew, so their output skew is the
// default #0 (14.3); only q has `output #2`.  Drives, by statement time T (an edge), cycle delay N:
//   edge 5 : d <= ##2 11   -> edge 25
//   edge 15: v <= 1        -> 15;              d <= ##2 22 -> 35
//   edge 25: v <= ##3 0    -> 55;              send(33,3): d <= ##3 33 -> 55;  q <= ##1 a -> 35 + skew 2 = 37
// The change history of each variable is recorded separately (the order of same-time
// changes of different variables is not fixed).
`timescale 1ns/1ns
module svclk3;
  logic clk = 0;
  initial repeat (20) #5 clk = ~clk;
  logic [7:0] d; logic v; logic [3:0] q;
  int errors = 0;
  // history entries: {value, time}
  logic [39:0] dh[$], vh[$], qh[$];
  clocking cb @(posedge clk);
    output d, v;
    input #1 output #2 q;
    sequence s1; d == 8'h11 ##1 v; endsequence
  endclocking
  task automatic send(input [7:0] x, input int n);
    cb.d <= ##n x;
  endtask
  initial begin
    d = 0; v = 0; q = 0;
    @(cb);
    cb.d <= ##2 8'h11;
    if (d !== 8'h00) begin $error("d = %h right after the drive statement at 5", d); errors++; end
    @(cb);
    cb.v <= 1;
    cb.d <= ##2 8'h22;
    @(cb);
    cb.v <= ##3 0;
    send(8'h33, 3);
    cb.q <= ##1 4'ha;
    @(cb); @(cb); @(cb); @(cb);
    #1;
    if (d !== 8'h33 || v !== 1'b0 || q !== 4'ha)
      begin $error("t=%0t end d=%h v=%b q=%h, expected 33 0 a", $time, d, v, q); errors++; end
    #5; if (d !== 8'h33 || v !== 1'b0 || q !== 4'ha) begin $error("t=%0t values changed", $time); errors++; end
    #5; if (d !== 8'h33 || v !== 1'b0 || q !== 4'ha) begin $error("t=%0t values changed", $time); errors++; end
    if ($time != 76) begin $error("final time %0t, expected 76", $time); errors++; end
    // change histories (changes at time 0 from the initial assignments are not recorded)
    if (dh.size() != 3 || dh[0] != {8'h11, 32'd25} || dh[1] != {8'h22, 32'd35} || dh[2] != {8'h33, 32'd55})
      begin $error("d history %p, expected (11@25, 22@35, 33@55)", dh); errors++; end
    if (vh.size() != 2 || vh[0] != {8'h01, 32'd15} || vh[1] != {8'h00, 32'd55})
      begin $error("v history %p, expected (1@15, 0@55)", vh); errors++; end
    if (qh.size() != 1 || qh[0] != {8'h0a, 32'd37})
      begin $error("q history %p, expected (a@37)", qh); errors++; end
    if (errors == 0) $display("PASSED");
  end
  always @(d) if ($time > 0) dh.push_back({d, 32'($time)});
  always @(v) if ($time > 0) vh.push_back({7'b0, v, 32'($time)});
  always @(q) if ($time > 0) qh.push_back({4'b0, q, 32'($time)});
endmodule
