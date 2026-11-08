// Expected values: IEEE 1800-2017 23.3.3.7 (port connections of unequal width: the net is connected to the
// low-order bits; surplus port bits are unconnected inside, surplus net bits have no driver from the port),
// 6.6.1 (resolution; z where nothing drives), 10.3 (assign). Case numbers refer to the three phases below.
`timescale 1ns/1ns
module buf1 #(parameter w = 4) (inout [w-1:0] a, b, input dir);
  assign b = dir ? a : 'bz;
  assign a = dir ? 'bz : b;
endmodule
module svinoutwidth1;
  wire [3:0] x;
  wire [2:0] y;
  wire [4:0] p;
  wire [3:0] q;
  reg [3:0] rx;
  reg [4:0] rq;
  reg dir;
  int err = 0;
  assign x = dir ? rx : 4'bz;
  assign q = dir ? 4'bz : rq[3:0];
  buf1 #(4) n (x, y, dir);
  buf1 #(4) m (p, q, dir);
  `define CHK(tag, ex, ey, ep, eq) if (x !== ex || y !== ey || p !== ep || q !== eq) begin err++; \
    $error("%s: x=%b y=%b p=%b q=%b", tag, x, y, p, q); end
  initial begin
    dir = 1; rx = 4'b1011; rq = 5'b10101;
    #1 `CHK("1", 4'b1011, 3'b011, 5'bzzzzz, 4'bzzzz)
    dir = 0;
    #1 `CHK("2", 4'bzzzz, 3'bzzz, 5'bz0101, 4'b0101)
    dir = 1; rx = 4'b0110;
    #1 `CHK("3", 4'b0110, 3'b110, 5'bzzzzz, 4'bzzzz)
    if (err == 0) $display("PASSED");
  end
endmodule
