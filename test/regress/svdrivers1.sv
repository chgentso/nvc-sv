`define first "complete"
module svdrivers1;
  parameter SEL = 1;
  enum logic [3:0] {a = 0, b = 4'bx, c = 4'bz, d = 3} e;
  reg [3:0] bits;
  int v, w;
  logic x;
  wire clk = 0;

  assign bits[1:0] = 2'b10;
  initial bits[3] = 1;

  if (SEL) begin : g1
    assign v = 12;
  end
  else begin : g2
    always @(posedge clk) v <= 1;
  end

  always @(posedge clk) w <= 5;
  assign x = 1;

  initial begin
    #1;
    if (bits[1:0] !== 2'b10 || bits[3] !== 1'b1)
      $fatal(1, "bits = %b", bits);
    if (v !== 12) $fatal(1, "v = %0d", v);
    e = b;
    if (e !== 4'bx) $fatal(1, "e = %b", e);
    $display("%s", `first);
    $display("PASSED");
  end
endmodule
