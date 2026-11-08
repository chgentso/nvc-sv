module inval5;
  enum integer {a=0, b={32{1'bx}}, c} e1;
  enum logic [3:0] {d=0, f=4'bx, g=1} e2;
  enum bit [1:0] {j={2{1'bx}}} e4;
  wire w;
  reg [3:0] mix1;
  int v, vn;
  logic x;
  reg r1, r2;
  assign v = 12;
  always @(posedge w) v <= ~v;
  assign mix1[1:0] = 0;
  initial mix1[3] = 1;
  assign {x, vn} = 0;
  always @(w) x = 1;
  assign r1 = 0;
  assign r1 = 1;
  always @(w) r2 = 1;
  initial r2 = 0;
  if (w) begin : g1
    assign r2 = 0;
  end
  specparam sp = 1;
  parameter pp = sp + 1;
endmodule
