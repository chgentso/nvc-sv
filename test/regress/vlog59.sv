// Variables and subprograms outside any module
int seed = 7;

function automatic int bump(int by);
   seed = seed + by;
   return seed;
endfunction

task automatic waitfor(int n);
   #(n);
   seed++;
endtask

package pkg1;
   int count = 3;
   function automatic int twice(int x);
      return x * 2 + count;
   endfunction
endpackage

import pkg1::*;

module vlog59;
   initial begin
      if (bump(3) != 10) $display("FAILED func");
      waitfor(2);
      if (seed != 11) $display("FAILED task %0d", seed);
      if (twice(4) != 11) $display("FAILED pkg");
      $display("PASSED");
   end
endmodule
