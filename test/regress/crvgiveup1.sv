// A search that runs out of work warns with the place of the randomize
// call and returns 0.  NVC_CRV_BUDGET makes the limit small enough to
// reach with a problem that has solutions.
class item;
   rand bit [15:0] d[64];
   constraint c { unique {d}; foreach (d[i]) d[i] < 70; }
endclass

module crvgiveup1;
   item it;
   initial begin
      it = new;
      if (it.randomize())
         $display("ok");
      else
         $display("no solution");
      $finish;
   end
endmodule
