// Self-checking. IEEE 1800-2017 11.8.1: the context of an assignment gives the
// width but not the signedness of the expression, also for initial values of
// nets and variables and parameters.
module svinitsign1;
  reg signed [3:0] a = -4, b = 3;
  wire [7:0] c = a / b;
  wire [7:0] d = a >>> 1;
  reg [7:0] v = a / b;
  reg [7:0] u = a >>> 1;
  localparam signed [3:0] LA = -4;
  localparam [7:0] LP = LA >>> 1;
  localparam [7:0] LQ = (LA >>> 1) + 8'b0;
  initial begin
    #1;
    if (c !== 8'hff) $error("FAIL c %h", c);
    if (d !== 8'hfe) $error("FAIL d %h", d);
    if (v !== 8'hff) $error("FAIL v %h", v);
    if (u !== 8'hfe) $error("FAIL u %h", u);
    if (LP !== 8'hfe) $error("FAIL LP %h", LP);
    if (LQ !== 8'h06) $error("FAIL LQ %h", LQ);
    $display("PASSED");
  end
endmodule
