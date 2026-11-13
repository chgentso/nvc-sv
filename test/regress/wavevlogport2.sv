`timescale 1fs/1fs
module wavevlogport2_sub(input a, input [3:0] hi, input [3:0] lo, output [3:0] z);
  assign z = hi ^ lo;
endmodule

module wavevlogport2;
  reg [7:0] w = 8'h5a;
  wire [3:0] o;
  wavevlogport2_sub s(w[0], w[7:4], w[3:0], o);
  initial begin
    #1 w = 8'h31;
    #1 w = 8'hf0;
    #1 $display("PASSED");
  end
endmodule
