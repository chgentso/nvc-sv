module sva8;
  logic clk = 0, a = 0, b = 0;
  always #5 clk = ~clk;
  always @(posedge clk) begin
    assert property (a |-> b) $display("%0t: pr pass", $time); else $display("%0t: pr FAIL", $time);
  end
  initial begin
    @(negedge clk); a = 1; b = 0;
    @(negedge clk); a = 1; b = 1;
    @(negedge clk); a = 0; b = 0;
    @(negedge clk); $finish;
  end
endmodule
