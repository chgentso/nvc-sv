module lintvlogfix2_sub(input [3:0] a, output [3:0] y);
  assign y = a;
endmodule

module lintvlogfix2;
  reg [7:0] r8;
  reg [3:0] r4;
  wire [3:0] w4;
  wire [3:0] y;
  wire [0:7] up;
  always @* begin
    r4 = r8;
    r4 = r8 + 1;
  end
  assign w4 = r8;
  lintvlogfix2_sub u(.a(r8), .y(y));
endmodule
