// A delayed non-blocking assignment takes effect in the non-blocking
// region of the time step together with the one that toggles the clock
// Expected values: IEEE 1800-2017 4.4 / 4.5 (the delayed r <= #10 and the
// clock toggle clk <= ~clk both become NBA updates of time 20 and are applied
// together, before the design that samples them runs), 10.4.2 (nonblocking
// assignment with intra-assignment delay).  The VHDL design (two-stage
// a -> ad pipeline) therefore sees r = 1 at the clock edge at time 20 while
// its internal ad is still 0: q = "10" at 20, and "11" after the edge at 30.
module svnbamix1;
  reg clk = 1;
  reg trig = 0;
  reg r = 0;
  wire [1:0] q;
  integer errors = 0;
  always #5 clk <= ~clk;
  initial #10 trig = 1;
  always @(posedge trig) r <= #10 1;
  svnbamix1_dut d (.clk(clk), .a(r), .q(q));
  initial begin
    #21 if (q !== 2'b10) begin $error("q at 21 is %b expected 10", q); errors = errors + 1; end
    #10 if (q !== 2'b11) begin $error("q at 31 is %b expected 11", q); errors = errors + 1; end
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
