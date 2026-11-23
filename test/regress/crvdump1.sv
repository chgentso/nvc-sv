class C;
   rand bit [7:0] a, b;
   rand int unsigned c;
   rand bit [15:0] d;
   rand bit [3:0] e[3];
   constraint k {
      a + b == 8'd100; c < 1000; c % 7 == 3; a > b; d == a * b;
      a inside {[10:90]};
      unique {e[0], e[1], e[2]};
      e.sum() with (int'(item)) > 10;
   }
endclass

module crvdump1;
   C o = new;
   initial begin
      repeat (3) begin
         if (!o.randomize()) $display("fail");
         $display("%0d %0d %0d %0d", o.a, o.b, o.c, o.d);
      end
   end
endmodule
