// A combinational island output read by other islands and also by a clock
// watch, a process and a force: the island readers take the value directly
// while the kernel signal stays published
module dpub_src (input logic a, en, input logic [7:0] v,
                 output logic c, output logic [7:0] w);
  assign c = a & en;
  assign w = v + 8'd1;
endmodule

module dpub_rd (input logic c, input logic [7:0] w,
                output logic d, output logic [7:0] x);
  assign d = ~c;
  assign x = w ^ 8'h0f;
endmodule

module vlog_island_dpub1;
  logic a = 0, en = 1;
  logic [7:0] v = 0;
  wire c, d, d2;
  wire [7:0] w, x, x2;
  logic q = 0;

  dpub_src u_src (.a, .en, .v, .c, .w);
  dpub_rd  u_rd1 (.c, .w, .d, .x);
  dpub_rd  u_rd2 (.c, .w, .d(d2), .x(x2));

  always_ff @(posedge c) q <= ~q;       // Clock watch on c

  always @(d or x or d2 or x2 or c)     // Process reading c and the readers
    if ($time > 0) $display("%0t: c=%b d=%b d2=%b x=%h x2=%h q=%b w=%h", $time, c, d, d2,
             x, x2, q, w);

  initial begin
    #1 a = 1;
    #1 a = 0;
    #1 v = 8'h10;
    #1 a = 1;
    #1 force u_src.w = 8'h33;           // Breaks the direct wire
    #1 a = 0;
    #1 v = 8'h20;
    #1 v = 8'h7f;
    #1 release u_src.w;
    #1 a = 0;
    #1 en = 0;
    #1 a = 1;
    #1 $finish;
  end
endmodule
