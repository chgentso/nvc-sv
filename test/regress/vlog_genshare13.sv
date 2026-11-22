// The branches of an if-generate in a shared loop have the same name:
// each branch taken by some iteration has its own body
module vlog_genshare13;
   logic clk = 0;
   logic [7:0] a = 8'h5a, q, y;

   for (genvar i = 0; i < 8; i++) begin
      if (i % 4 != 3) begin
         always_ff @(posedge clk) q[i] <= ~a[i];
         assign y[i] = q[i];
      end
      else begin
         always_ff @(posedge clk) if (clk) q[i] <= 0;
         assign y[i] = q[i] ^ 1'b1;
      end
   end

   for (genvar i = 0; i < 4; i++) begin : g
      if (i < 2) begin : b
         logic t;
         assign t = a[i];
      end
      else begin : b
         logic [1:0] t;
         assign t = {a[i], a[i]};
      end
   end

   initial begin
      #1 clk = 1;
      #1;
      if (q !== 8'b00100101 || y !== 8'b10101101)
         $display("FAILED %b %b", q, y);
      else if (g[1].b.t !== 1'b1 || g[2].b.t !== 2'b00 || g[3].b.t !== 2'b11)
         $display("FAILED %b %b %b", g[1].b.t, g[2].b.t, g[3].b.t);
      else
         $display("PASSED");
   end
endmodule
