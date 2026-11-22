// Names of the scopes of a shared loop
module vlog_genshare6;
   logic [3:0] v;
   for (genvar i = 0; i < 4; i++) begin : g
      logic t;
      assign t = v[i];
      initial begin : blk
         #(i + 1);
         $display("%m %0d %b", i, t);
      end
      always @(t) if ($time > 10) $display("%m %0d changed %b", i, t);
      if (i % 2 == 1) begin : odd
         initial #10 $display("%m %0d", i);
      end
   end
   initial begin
      v = 4'b1010;
      #20 v = 4'b0110;
      #1 $finish;
   end
endmodule
