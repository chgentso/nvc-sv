module linterr9(input logic [7:0] d);
  logic r;
  always @* begin
    r = d[0];
  logic [7:0] a8;
  logic [3:0] w4;
  always @* begin
    case (d)
      0: r = 1;
      1: r = 0;
  end
  assign w4 = d;
endmodule

module linterr9b(input logic [7:0] d);
  logic [3:0] v4;
  assign v4 = d;
endmodule
