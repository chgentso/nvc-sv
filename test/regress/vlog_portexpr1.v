module split(.hi(x[7:4]), .lo(x[3:0]), y);
  input [7:0] x;
  output [7:0] y;
  assign y = ~x;
endmodule

module join2({p, q}, r);
  input [1:0] p;
  input [1:0] q;
  output [3:0] r;
  assign r = {q, p};
endmodule

module outs(a, .pair({m, n}), .alias(inner), .top(w[3]));
  input [3:0] a;
  output [2:0] m;
  output n;
  output inner;
  output [3:0] w;
  reg [2:0] m;
  wire inner = a[0];
  assign w = a;
  always @* begin
    m = a[3:1];
  end
  assign n = ^a;
endmodule

module vlog_portexpr1;
  reg [3:0] h, l;
  wire [7:0] y;
  wire [3:0] r, pair;
  wire al, t;
  split u1(.hi(h), .lo(l), .y(y));
  join2 u2(2'b01, r);
  outs u3(.a(h), .pair(pair), .alias(al), .top(t));
  initial begin
    h = 4'ha; l = 4'h3;
    #1 $display("y=%h r=%h pair=%h al=%b t=%b", y, r, pair, al, t);
    h = 4'h5; l = 4'hc;
    #1 $display("y=%h r=%h pair=%h al=%b t=%b", y, r, pair, al, t);
  end
endmodule
