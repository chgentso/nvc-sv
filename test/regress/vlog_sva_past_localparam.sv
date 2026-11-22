module chk #(parameter int EXTRA = 1) (input logic clk, input logic [7:0] cnt);
  if (EXTRA > 0) begin : g
    localparam int unsigned N = 2 * EXTRA;
    int bad, good;
    // The number of ticks is a constant expression of a localparam
    assert property (@(posedge clk) cnt > 8 |-> cnt - $past(cnt, N + 1) == 8'(N + 1))
      good++;
    else
      bad++;
  end
endmodule

module vlog_sva_past_localparam;
  logic clk = 0;
  logic [7:0] cnt = 0;
  always #5 clk = ~clk;
  always @(posedge clk) cnt <= cnt + 1;
  chk #(.EXTRA(1)) u(.clk(clk), .cnt(cnt));
  initial begin
    #200;
    if (u.g.bad == 0 && u.g.good > 5) $display("PASSED");
    else $display("FAILED %0d %0d", u.g.bad, u.g.good);
    $finish;
  end
endmodule
