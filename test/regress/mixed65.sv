module mixed65_mon (
  input logic       clk,
  input logic       en,
  input logic [7:0] cnt
);
  int seen = 0;

  always_ff @(posedge clk) begin
    if (en)
      seen <= seen + 1;
  end

  final begin
    if (seen != 4 || cnt !== 8'd4)
      $fatal(1, "mixed65_mon seen=%0d cnt=%0d", seen, cnt);
  end
endmodule

module mixed65;
  logic clk = 0, en = 0;
  wire [7:0] cnt;
  always #5 clk = ~clk;

  mixed65_dut dut (.clk(clk), .en(en), .cnt(cnt));
  bind mixed65_dut mixed65_mon u_mon (.clk(clk), .en(en), .cnt(cnt));

  initial begin
    @(posedge clk);
    en = 1;
    repeat (4) @(posedge clk);
    en = 0;
    @(posedge clk);
    if (cnt !== 8'd4) $fatal(1, "cnt=%0d exp=4", cnt);
    $display("PASSED");
    $finish;
  end
endmodule
