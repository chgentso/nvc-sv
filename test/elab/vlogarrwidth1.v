// IEEE 1800-2017 23.3.3: a connection to a port of an array of instances
// is as wide as one port or all of the ports.
module leaf (input [0:0] a, input [0:0] b);
endmodule

module vlogarrwidth1;
   reg [3:0] wide = 4'b1010;
   reg [4:0] bad;

   leaf ok [3:0] (wide, 1'b1);
   leaf err [1:0] (wide[1:0], bad);
endmodule
