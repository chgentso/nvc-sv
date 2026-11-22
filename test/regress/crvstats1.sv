class item;
   rand bit [31:0] a;
   rand bit [7:0]  b;
   rand bit [15:0] c;
   rand int        d[];
   rand bit [3:0]  m;
   constraint k {
      a inside {[100:5000]};
      b < 200;
      c[11:4] == 8'h5a;
      d.size() inside {[1:5]};
      foreach (d[i]) d[i] inside {[0:9]};
      m dist {0 := 1, [1:3] :/ 6};
      (b > 100) -> (a[3:0] == 4'd3);
      soft m != 2;
   }
endclass

module crvstats1;
   item o;
   int  r;

   initial begin
      o = new;
      repeat (30) begin
         r = o.randomize();
         $display("%0d %0d %0d %0d %0p %0d", o.a, o.b, o.c, o.d.size(), o.d, o.m);
      end
      $finish;
   end
endmodule
