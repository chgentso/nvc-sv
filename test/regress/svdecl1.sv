// Declaration forms: macromodule, pragma, line, enum sequences, type
// parameters and for loops declaring several variables
`pragma foo something, somethingelse = 7, "abcdef", ( hello, ( "world", pr = 4 ) ), a
`line 1 "somefile" 2
macromodule svdecl1;
   `define CHECK(x, y) if ((x) !== (y)) begin \
      $display("FAILED %0d: %0d", y, (x)); $finish; end

   `pragma protect
   `line 5 "other" 0

   enum {start=10, stop[11:13], rep[3], rev[3:1]} e;
   typedef enum {a0=5, b[2], c} t_t;
   t_t x;
   localparam type big_t = logic [15:0];
   localparam type a_t = int, b_t = byte;
   big_t y;
   a_t ai;
   b_t bi;
   int n;

   initial begin
      e = stop12;
      `CHECK(start, 10);
      `CHECK(stop11, 11);
      `CHECK(stop13, 13);
      `CHECK(rep0, 14);
      `CHECK(rep2, 16);
      `CHECK(rev3, 17);
      `CHECK(rev1, 19);
      `CHECK(a0, 5);
      `CHECK(b0, 6);
      `CHECK(b1, 7);
      `CHECK(c, 8);
      `CHECK(e, 12);
      $display("%0d %0d", start, c);

      y = 16'hABCD; ai = -5; bi = 8'sd100;
      `CHECK(y, 16'hABCD);
      `CHECK(ai, -5);
      `CHECK(bi, 100);

      n = 0;
      for (int i = 0, j = 10, bit k = 1'b1; i < 3; i++, j--) begin
         n += j;
         `CHECK(k, i % 2 == 0 ? 1'b1 : 1'b0);
         k = ~k;
      end
      `CHECK(n, 27);
      for (byte b = 1, longint l = 5; b < 3; b++, l *= 2) n += l;
      `CHECK(n, 27 + 5 + 10);
      $display("PASSED");
   end
endmodule
