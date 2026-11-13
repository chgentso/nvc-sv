// Self-checking. IEEE 1800-2017 23.2.2.1: the net declared for a non-ANSI
// port may be signed even though the port declaration is not.
module svportsigned1_sub (a, b, o);
  input [7:0] a, b;
  output [15:0] o;
  wire signed [7:0] a;
  assign o = $signed({1'b0, b}) * a;
endmodule

module svportsigned1;
  reg [7:0] a = 8'hff, b = 2;
  wire [15:0] o;
  svportsigned1_sub s(a, b, o);
  initial begin
    #1;
    if (o !== 16'hfffe) $error("FAIL %h", o);
    else $display("PASSED");
  end
endmodule
