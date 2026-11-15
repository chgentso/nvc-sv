module sub #(parameter N = 1) (input clk, output reg [3:0] q);
   integer cnt;

   function int twice(input int x);
      return x * 2;
   endfunction

   initial begin : blk
      cnt = 0;
      q = 0;
   end

   always @(posedge clk) begin
      cnt <= cnt + 1;
      if (N > 1)
         q <= q + 2;
      else if (N > 0)
         q <= q + 1;
      case (cnt)
         0: q <= 0;
         default: q <= q;
      endcase
   end
endmodule

module top;
   reg clk = 0;
   wire [3:0] q0, q1, q2;

   sub #(.N(1)) u1 (.clk(clk), .q(q0));
   sub #(.N(2)) u2 (.clk(clk), .q(q1));

   for (genvar i = 0; i < 2; i++) begin : g
      sub #(.N(i)) u (.clk(clk), .q(q2));
   end

   always #5 clk = ~clk;
endmodule
