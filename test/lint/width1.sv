module wsub(input [3:0] a, output [7:0] y);
  assign y = {4'b0, a};
endmodule

module width1;
  reg [7:0] r8;
  reg [3:0] r4;
  reg [3:0] q4;
  wire [7:0] w8;
  wire [3:0] w4;
  wire [3:0] w4b;
  integer i;
  localparam P = 5;
  always @* begin
    r4 = r8;            // truncates
    r4 = r8[3:0];       // part select
    r4 = 4'(r8);        // explicit cast
    r4 = 3;             // constant that fits
    r4 = 8'd200;        // constant that does not fit
    r4 = 8'd5;          // sized constant that fits
    r4 = P;             // parameter that fits
    r4 = r4 + 1;        // unsized literal
    r4 = r8 + r4;       // wider operand
    r8 = r4;            // extension
    r4 = r8 == 0;       // one bit result
    r4 = {r4, r4};      // concatenation
    r4 = r8; // nvc lint_off width
    // nvc lint_off width
    q4 = r8;
    r4 = i;             // integer is 32 bits
    r4 = 32'bx;         // unknown value fits anywhere
  end
  assign w4 = w8;
  wsub u1(.a(r8), .y(w4b));
endmodule
