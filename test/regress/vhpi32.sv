module vhpi_cpu (input clk, output [31:0] addr);
   reg [31:0] n = 0;
   wire [31:0] a1;
   assign a1 = n * 4;
   assign addr = a1;
   always @(posedge clk) n <= (n + 1) % 16;
endmodule
