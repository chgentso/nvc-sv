// A slice of an unpacked array held in memory, such as the member of a
// structure, is stored to and loaded from the selected elements
// (IEEE 1800-2017 7.4.3, 11.5.1).

module vlogslice1;
   typedef struct { logic [31:0] arr[0:3]; } up_t;
   typedef struct { logic [31:0] arr[3:0]; } down_t;

   up_t up, up2;
   down_t down, down2;

   initial begin
      up.arr[0] = 1;
      up.arr[1:3] = '{4, 3, 2};
      if (up.arr[0] !== 1 || up.arr[1] !== 4 || up.arr[2] !== 3
          || up.arr[3] !== 2)
         $fatal(1, "up pattern %0d %0d %0d %0d", up.arr[0], up.arr[1],
                up.arr[2], up.arr[3]);

      up2.arr = '{9, 9, 9, 9};
      up2.arr[1:3] = up.arr[1:3];
      if (up2.arr[0] !== 9 || up2.arr[1] !== 4 || up2.arr[2] !== 3
          || up2.arr[3] !== 2)
         $fatal(1, "up copy %0d %0d %0d %0d", up2.arr[0], up2.arr[1],
                up2.arr[2], up2.arr[3]);

      down.arr = '{1, 2, 3, 4};
      down.arr[2:0] = '{7, 8, 9};
      if (down.arr[3] !== 1 || down.arr[2] !== 7 || down.arr[1] !== 8
          || down.arr[0] !== 9)
         $fatal(1, "down pattern %0d %0d %0d %0d", down.arr[3], down.arr[2],
                down.arr[1], down.arr[0]);

      down2.arr = '{1, 2, 3, 4};
      down2.arr[2:1] = down.arr[1:0];
      if (down2.arr[3] !== 1 || down2.arr[2] !== 8 || down2.arr[1] !== 9
          || down2.arr[0] !== 4)
         $fatal(1, "down copy %0d %0d %0d %0d", down2.arr[3], down2.arr[2],
                down2.arr[1], down2.arr[0]);

      down2.arr[0 +: 2] = '{5, 6};
      if (down2.arr[1] !== 5 || down2.arr[0] !== 6 || down2.arr[2] !== 8)
         $fatal(1, "indexed %0d %0d %0d", down2.arr[2], down2.arr[1],
                down2.arr[0]);
      $display("PASSED");
      $finish;
   end
endmodule
