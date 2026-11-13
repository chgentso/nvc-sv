module lintvlogrules1(input clk, input [1:0] sel, input d, output reg y);
  wire n1;
  reg a, b, c, e;
  reg [1:0] f;
  sub u(.a(d), .y(implicit_w));              // implicit net
  assign other_w = d;                         // nvc lint_off implicit-net
  assign n1 = d;
  always @(posedge clk) begin
    a <= d;
    b = d;
  end
  always @(posedge clk) b <= n1;              // b has both kinds
  always @(posedge clk) c <= d;
  always @(posedge clk) e = d;
  always @(*) begin
    y = 0;
    case (sel)                                // no default
      2'd0: y = a;
      2'd1: y = b;
    endcase
  end
  always @(*) begin
    f = 0;
    case (sel)
      2'd0: f = 1;
      default: f = 2;
    endcase
    case (sel)
      2'd0, 2'd1, 2'd2, 2'd3: f = 3;          // covers every value
    endcase
  end
  always @(posedge clk) begin
    case (sel)
      2'd0: c <= 1;
    endcase
  end
endmodule

module sub(input a, output y);
  assign y = a;
endmodule
