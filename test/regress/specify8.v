// IEEE 1800-2017 31.6: a timing check violation toggles its notifier:
// x becomes 0, 0 becomes 1, 1 becomes 0 and z stays z.  Applies to
// $setup, $hold, $recovery and $removal (31.3.1-31.3.4).
`timescale 1ns/1ns

module specify8;
  reg d = 0, clk = 0, rst = 0;
  integer errors = 0;

  sub i(.d(d), .clk(clk), .rst(rst));

  initial begin
    #4 d = 1;              // setup violation at 5
    #1 clk = 1;
    #1 d = 0;              // hold violation at 6
    #4 clk = 0;
    #4 d = 1;              // setup violation at 15
    #1 clk = 1;
    #1 clk = 0; rst = 1;   // hold violation and rst at 16
    d = 0;
    #1 clk = 1;            // setup and recovery violations at 17
    #10;
    if (i.ns !== 1'b0) begin $error("setup notifier %b, expected 0 (x, 0, 1, 0)", i.ns); errors++; end
    if (i.nh !== 1'b0) begin $error("hold notifier %b, expected 0 (0, 1, 0)", i.nh); errors++; end
    if (i.nz !== 1'bz) begin $error("z notifier %b, expected z", i.nz); errors++; end
    if (i.nr !== 1'b1) begin $error("recovery notifier %b, expected 1", i.nr); errors++; end
    if (i.cnt != 3) begin $error("setup notifier changed %0d times, expected 3", i.cnt); errors++; end
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule

module sub(input d, input clk, input rst);
  reg ns, nh = 0, nz = 1'bz, nr = 0;
  integer cnt = 0;
  always @(ns) cnt++;

  specify
    $setup(d, posedge clk, 2, ns);
    $hold(posedge clk, d, 2, nh);
    $setup(d, posedge clk, 2, nz);
    $recovery(posedge rst, posedge clk, 2, nr);
  endspecify
endmodule
