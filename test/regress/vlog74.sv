// Multi-dimensional unpacked arrays in modules, locals and class properties
module vlog74;
   class c;
      int a[4];
      int m[3][2];
      static int s[2];
   endclass

   int mv[3][2];
   reg [7:0] mr[0:1][1:3];
   int down[3:1];

   `define CHECK(x, y) if ((x) !== (y)) begin \
      $display("FAILED %0d: %0d", y, (x)); $finish; end

   initial begin
      automatic int l[2][3];
      automatic c o = new;
      automatic int sum = 0;

      for (int i = 0; i < 3; i++)
        for (int j = 0; j < 2; j++)
          mv[i][j] = i * 10 + j;

      for (int i = 0; i < 3; i++)
        for (int j = 0; j < 2; j++)
          sum += mv[i][j];
      `CHECK(sum, 63);
      `CHECK(mv[2][1], 21);
      `CHECK(mv[1][0], 10);

      for (int i = 0; i < 2; i++)
        for (int j = 0; j < 3; j++)
          l[i][j] = i * 100 + j;
      `CHECK(l[1][2], 102);
      `CHECK(l[0][1], 1);

      for (int i = 0; i < 2; i++)
        for (int j = 1; j <= 3; j++)
          mr[i][j] = i * 16 + j;
      `CHECK(mr[1][3], 8'h13);
      `CHECK(mr[0][1], 8'h01);

      down[3] = 7;
      down[1] = 9;
      `CHECK(down[3], 7);
      `CHECK(down[1], 9);

      for (int i = 0; i < 4; i++)
        o.a[i] = i + 40;
      `CHECK(o.a[0], 40);
      `CHECK(o.a[3], 43);

      for (int i = 0; i < 3; i++)
        for (int j = 0; j < 2; j++)
          o.m[i][j] = i * 10 + j;
      `CHECK(o.m[2][1], 21);
      `CHECK(o.m[0][1], 1);

      o.a[o.a[0] - 39]++;
      `CHECK(o.a[1], 42);

      foreach (mv[i, j])
        mv[i][j] = -(i * 10 + j);
      `CHECK(mv[2][1], -21);

      $display("PASSED");
      $finish;
   end
endmodule
