// Expected values: IEEE 1800-2017 23.3.3.5 (instance array port connections: wide connection split with
// the leftmost instance taking the leftmost bits, narrow connection replicated), 11.4.12 (concatenation)
`timescale 1ns/1ns
module svinstarr2_leaf(input logic [3:0] a, input logic b, output logic [3:0] o);
  assign o = a ^ {4{b}};
endmodule
module svinstarr2;
  logic [3:0] x = 4'h5;
  logic [3:0] z = 4'h3;
  logic y = 1;
  wire [7:0] o2;
  wire [11:0] o3;
  wire [15:0] o4;
  int errs = 0;
  svinstarr2_leaf u[1:0] (.a({2{x}}), .b(y), .o(o2));
  svinstarr2_leaf v[2:0] (.a({x, x, x}), .b({1'b0, 1'b1, 1'b0}), .o(o3));
  svinstarr2_leaf w[3:0] (.a({2{x, z}}), .b({2{y, 1'b0}}), .o(o4));
  initial begin
    #1;
    // u: both 5^f=a; v[2]=5 v[1]=a v[0]=5; w[3]=5^f, w[2]=3, w[1]=5^f, w[0]=3
    if (o2 !== 8'haa) begin errs++; $error("o2=%h", o2); end
    if (o3 !== 12'h5a5) begin errs++; $error("o3=%h", o3); end
    if (o4 !== 16'ha3a3) begin errs++; $error("o4=%h", o4); end
    x = 4'ha; #1;
    if (o2 !== 8'h55) begin errs++; $error("o2=%h", o2); end
    if (o3 !== 12'ha5a) begin errs++; $error("o3=%h", o3); end
    if (o4 !== 16'h5353) begin errs++; $error("o4=%h", o4); end
    if (errs == 0) $display("PASSED");
  end
endmodule
