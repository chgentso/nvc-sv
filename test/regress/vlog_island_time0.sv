// Without islands an always_comb block first runs at time zero, so its
// outputs and the continuous assignments computed from them change at
// time zero.  An RTL island must not publish these values earlier: a
// process waiting for a change of them must see it at time zero.

module vlog_island_time0_a (input logic clk, input logic [7:0] i,
                            output logic [7:0] p, output wire [7:0] u,
                            output logic [7:0] q);
   always_comb p = i + 8'd1;
   assign u = p + 8'd1;
   always_ff @(posedge clk) q <= u;
endmodule

module vlog_island_time0;
   logic       clk = 0;
   logic [7:0] din = 8'h00;
   logic [7:0] p, q;
   wire [7:0]  u;

   vlog_island_time0_a ua (.clk(clk), .i(din), .p(p), .u(u), .q(q));

   always @(u) $display("%0t: u=%h", $time, u);

   initial begin
      #1 din = 8'h10;
      #1 clk = 1;
      #1 $display("%0t: q=%h", $time, q);
      $finish;
   end
endmodule
