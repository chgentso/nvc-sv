class Mul16;
   rand bit [15:0] x, y;
   constraint c { x * y == 16'd30031; x > 1; y > 1; }
endclass

class Mul32;
   rand bit [31:0] x, y;
   constraint c { x * y == 32'd7000021; x < 100000; y < 100000000; }
endclass

class Mul32Const;
   rand bit [31:0] x, y;
   constraint c { x * 32'd12345 == y; y < 1000; x > 5; }
endclass

class BitRel;
   rand bit [31:0] x, y;
   constraint c { (x & y) == 32'h0000ff00; (x | y) == 32'hffffff00; }
endclass

class Count32;
   rand bit [31:0] x, y;
   constraint c { (x & y) == 0; x != 0; y != 0; $countones(x | y) == 32; }
endclass

class SubMask;
   rand bit [31:0] x, m;
   constraint c { (x & m) == x; $countones(m) == 4; x != 0; }
endclass

class Slice108;
   rand bit [107:0] v;
   constraint c { v[71:54] == 18'h1234; }
endclass

class Wide256;
   rand bit [255:0] x, y;
   constraint c { x != y; (x & y) == 0; }
endclass

class Wide1024;
   rand bit [1023:0] m;
   bit [9:0] n = 3;
   int width = 39;
   constraint c { $countones(m) == n; (m >> width) == '0; }
endclass

class UnsatMul;
   rand bit [7:0] x, y;
   constraint c { x > 1; y > 1; x < 17; y < 17; x * y == 8'd17; }
endclass

module crvsat1;
   initial begin
      automatic Mul16 m16 = new;
      automatic Mul32 m32 = new;
      automatic Mul32Const m32c = new;
      automatic BitRel br = new;
      automatic Count32 c32 = new;
      automatic SubMask sm = new;
      automatic Slice108 s108 = new;
      automatic Wide256 w256 = new;
      automatic Wide1024 w1024 = new;
      automatic UnsatMul um = new;

      repeat (20) begin
         if (!m16.randomize()) $fatal(1, "Mul16 failed");
         if (m16.x * m16.y != 16'd30031 || m16.x <= 1 || m16.y <= 1)
            $fatal(1, "Mul16 bad: %0d * %0d", m16.x, m16.y);
      end

      repeat (10) begin
         if (!m32.randomize()) $fatal(1, "Mul32 failed");
         if (m32.x * m32.y != 32'd7000021 || m32.x >= 100000 || m32.y >= 100000000)
            $fatal(1, "Mul32 bad: %0d * %0d", m32.x, m32.y);
      end

      repeat (20) begin
         if (!m32c.randomize()) $fatal(1, "Mul32Const failed");
         if (m32c.x * 32'd12345 != m32c.y || m32c.y >= 1000 || m32c.x <= 5)
            $fatal(1, "Mul32Const bad");
      end

      repeat (20) begin
         if (!br.randomize()) $fatal(1, "BitRel failed");
         if ((br.x & br.y) != 32'h0000ff00 || (br.x | br.y) != 32'hffffff00)
            $fatal(1, "BitRel bad");
      end

      repeat (20) begin
         if (!c32.randomize()) $fatal(1, "Count32 failed");
         if ((c32.x & c32.y) != 0 || c32.x == 0 || c32.y == 0
             || $countones(c32.x | c32.y) != 32)
            $fatal(1, "Count32 bad");
      end

      repeat (20) begin
         if (!sm.randomize()) $fatal(1, "SubMask failed");
         if ((sm.x & sm.m) != sm.x || $countones(sm.m) != 4 || sm.x == 0)
            $fatal(1, "SubMask bad");
      end

      repeat (20) begin
         if (!s108.randomize()) $fatal(1, "Slice108 failed");
         if (s108.v[71:54] != 18'h1234)
            $fatal(1, "Slice108 bad");
      end

      repeat (20) begin
         if (!w256.randomize()) $fatal(1, "Wide256 failed");
         if (w256.x == w256.y || (w256.x & w256.y) != 0)
            $fatal(1, "Wide256 bad");
      end

      repeat (10) begin
         if (!w1024.randomize()) $fatal(1, "Wide1024 failed");
         if ($countones(w1024.m) != 3 || (w1024.m >> 39) != '0)
            $fatal(1, "Wide1024 bad");
      end

      if (um.randomize()) $fatal(1, "UnsatMul unexpectedly succeeded");

      $display("crvsat1 done");
      $finish;
   end
endmodule
