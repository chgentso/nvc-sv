// Multiplication, division and remainder of values wider than 64 bits
class W;
  rand bit [127:0] x;
  rand bit [127:0] y;
  rand bit signed [95:0] s;
  rand bit signed [95:0] t;
  constraint c1 { y inside {[128'd2:128'd9]}; }
  constraint c2 { x * 128'd1000003 == 128'd1000003 * 128'd123456789012345678; x * y > 128'd0; }
  constraint c3 { s / t == -96'sd7; t > 96'sd0 && t < 96'sd100; s % t == -96'sd3; }
endclass

class V;
  rand bit [199:0] a;
  rand bit [199:0] b;
  constraint c { a / b == 200'd5; a % b == 200'd2; b > 200'd10; b < 200'd1000; }
endclass

module crvwidearith1;
  initial begin
    automatic W w = new;
    automatic V v = new;
    for (int i = 0; i < 6; i++) begin
      if (!w.randomize()) $fatal(1, "W randomize failed");
      if (w.x != 128'd123456789012345678 || w.y < 2 || w.y > 9)
        $fatal(1, "product %0d %0d", w.x, w.y);
      if (w.s / w.t != -96'sd7 || w.s % w.t != -96'sd3)
        $fatal(1, "signed %0d %0d", w.s, w.t);
      if (!v.randomize()) $fatal(1, "V randomize failed");
      if (v.a / v.b != 5 || v.a % v.b != 2) $fatal(1, "divide %0d %0d", v.a, v.b);
    end
    $display("PASSED");
  end
endmodule
