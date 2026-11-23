module mixedcover3_sub (input a, output reg [1:0] y);
   reg last;
   always @(a) begin
      y = {a, ~a};
      last = a;
   end
endmodule
