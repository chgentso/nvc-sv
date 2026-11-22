// The elements decide which sizes of a dynamic array are possible: with
// elements below 50 a sum of 200 needs five or more of them
class item;
   rand bit [7:0] d[];
   rand bit [7:0] total;
   constraint c {
      d.size() inside {[1:10]};
      d.sum() == total;
      total == 200;
      foreach (d[i]) d[i] < 50;
   }
endclass

module crvsize1;
   item it;
   int ok, s;
   initial begin
      it = new;
      ok = 0;
      for (int i = 0; i < 200; i++) begin
         if (it.randomize()) begin
            s = 0;
            foreach (it.d[j]) s += it.d[j];
            if ((s & 255) == 200 && it.d.size() >= 5)
               ok++;
         end
      end
      $display("ok=%0d", ok);
      $finish;
   end
endmodule
