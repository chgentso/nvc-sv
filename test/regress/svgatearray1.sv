// Arrays of gate instances, see IEEE 1800-2017 section 28.3.6

module inv #(parameter N = 3) (input [N-1:0] a, output [N-1:0] y);
  not n [N-1:0] (y, a);
endmodule

module svgatearray1;
  wire [3:0] pad, inv4;
  wire [0:3] asc;
  wire [2:0] iy;
  wire [1:0] hi;
  wire x, y;
  reg  [3:0] d = 4'b1010, en = 4'b1111;
  reg  [2:0] ia = 3'b110;
  reg        e1 = 1;
  reg  [1:0] c0 = 2'b10;

  bufif1 b [3:0] (pad, d, en);
  not    n [3:0] (inv4, d);
  buf    a [0:3] (asc, d);
  bufif0 z [1:0] (hi, c0, 1'b0);
  and    g [1:0] ({x, y}, c0, {e1, e1});
  inv #(3) u (ia, iy);

  initial begin
    #1;
    if (pad !== 4'b1010 || inv4 !== 4'b0101 || asc !== 4'b1010
        || hi !== 2'b10 || {x, y} !== 2'b10 || iy !== 3'b001) begin
      $display("FAILED 1: %b %b %b %b %b%b %b", pad, inv4, asc, hi, x, y, iy);
      $finish;
    end
    en = 4'b0110;
    d = 4'b0101;
    #1;
    if (pad !== 4'bz10z || inv4 !== 4'b1010 || asc !== 4'b0101) begin
      $display("FAILED 2: %b %b %b", pad, inv4, asc);
      $finish;
    end
    $display("PASSED");
  end
endmodule
