module mixed61;
  logic clk = 0;
  always #5 clk = ~clk;
  wire [15:0] o;

  mixed61_e dut (.clk(clk), .o(o));

  initial begin
    @(posedge clk); #1;
    if ($signed(o) !== 16'sd5) $fatal(1, "init o=%0d exp=5", $signed(o));

    dut.r.bias = 100;
    dut.cnt = 20;
    @(posedge clk); #1;
    if ($signed(o) !== 16'sd120) $fatal(1, "after write o=%0d exp=120", $signed(o));

    force dut.cnt = -10;
    @(posedge clk); #1;
    if ($signed(o) !== 16'sd90) $fatal(1, "after force o=%0d exp=90", $signed(o));
    @(posedge clk); #1;
    if ($signed(o) !== 16'sd90) $fatal(1, "force did not hold o=%0d", $signed(o));

    release dut.cnt;
    @(posedge clk); #1;
    @(posedge clk); #1;
    if ($signed(o) === 16'sd90) $fatal(1, "release did not resume");

    $display("PASSED");
    $finish;
  end
endmodule
