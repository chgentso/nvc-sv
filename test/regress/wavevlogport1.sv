`timescale 1fs/1fs
module wavevlogport1_stage(input clk, input [3:0] in_word, output [3:0] out_word);
  reg [3:0] r;
  always @(posedge clk) r <= in_word;
  assign out_word = r;
endmodule

module wavevlogport1_pair(input clk, input [3:0] a, output [3:0] z);
  wire [3:0] mid;
  wavevlogport1_stage s1(clk, a, mid);
  wavevlogport1_stage s2(.clk(clk), .in_word(mid), .out_word(z));
endmodule

module wavevlogport1;
  reg clk = 0;
  reg [3:0] w = 0;
  wire [3:0] o;
  wavevlogport1_pair p(clk, w, o);
  initial begin
    #1 clk = 1;
    #1 clk = 0;
    #1 w = 4'h5;
    #1 clk = 1;
    #1 clk = 0;
    #1 clk = 1;
    #1 $display("PASSED");
  end
endmodule
