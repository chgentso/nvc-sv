// Unpacked arrays connected to VHDL ports of array of integer type
module mixed34;
   int a[4] = '{10, 20, 30, -40};
   int b[4];
   int c[4];
   int d[4] = '{7, 8, 9, 5};

   mixed34_sub u (.i(a), .o(b), .od(c), .io(d));

   initial begin
      #1;
      if (b[0] !== -40 || b[1] !== 30 || b[2] !== 20 || b[3] !== 10)
        $display("FAILED b %0d %0d %0d %0d", b[0], b[1], b[2], b[3]);
      else if (c[0] !== 5 || c[1] !== 9 || c[2] !== 8 || c[3] !== 8)
        $display("FAILED c %0d %0d %0d %0d", c[0], c[1], c[2], c[3]);
      else
        $display("PASSED");
      $finish;
   end
endmodule
