// Continuous assignments and output ports to members of an unpacked structure
// including selects, concatenations, and real members
// Expected values: IEEE 1800-2017 10.3 (continuous assignment to variables, including
// member/part selects and concatenations), 23.3.3 (output port connections are
// continuous assignments), 11.4.12 (concatenation), 7.2 (structure members),
// 6.12 (real arithmetic: i * 0.5)
typedef struct { logic [7:0] x; logic [7:0] y; real r; } d_t;
module svstructconn2_sub(input [7:0] i, output [7:0] o, output real ro);
  assign o = i + 1; assign ro = i * 0.5;
endmodule
module svstructconn2;
  int errs;
  task automatic ck(string tag, logic [15:0] got, logic [15:0] exp);
    if (got !== exp) begin $error("%s: got %h expected %h", tag, got, exp); errs++; end
  endtask
  task automatic ckr(string tag, real got, real exp);
    if (!(got == exp)) begin $error("%s: got %f expected %f", tag, got, exp); errs++; end
  endtask
  d_t s;
  logic [7:0] src = 3;
  assign s.x = src + 1;
  assign s.y = src + 2;
  initial begin
    #1 ck("s t1 x", s.x, 4); ck("s t1 y", s.y, 5);
    src = 10;
    #1 ck("s t2 x", s.x, 11); ck("s t2 y", s.y, 12);
  end
  d_t t;
  svstructconn2_sub u(.i(src), .o(t.x), .ro(t.r));
  initial begin #3 ck("t.x", t.x, 11); ckr("t.r", t.r, 5.0); end
  d_t q;
  logic [3:0] a, b;
  wire [3:0] hi = src[7:4];
  assign q.x[3:0] = 4'd5;
  assign {q.x[7:4], q.y} = {4'd9, 8'd7};
  initial #3 begin ck("q.x", q.x, 8'h95); ck("q.y", q.y, 8'h07); end
  d_t w;
  svstructconn2_sub u2(.i(src), .o({a, b}), .ro());
  svstructconn2_sub u3(.i(src), .o(w.y), .ro());
  initial begin d_t z; z.x = 1; w.x = 2; #4 ck("w.x", w.x, 2); ck("w.y", w.y, 11); ck("a", a, 0); ck("b", b, 11); end
  // procedural write to another member of struct driven by assign
  d_t m;
  assign m.x = 8'd55;
  initial begin m.y = 8'd66; #5 ck("m.x", m.x, 55); ck("m.y", m.y, 66); end
  // concat output port
  d_t c;
  svstructconn2_sub u4(.i(src), .o(c.x), .ro(c.r));
  svstructconn2_sub u5(.i(8'd1), .o({c.y[3:0], c.y[7:4]}), .ro());
  initial begin
    #6 ck("c.x", c.x, 8'h0b); ck("c.y", c.y, 8'h20); ckr("c.r", c.r, 5.0);
    if (errs == 0) $display("PASSED");
  end
endmodule
