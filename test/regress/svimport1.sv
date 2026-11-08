// Package import inside a module body
package imppkg;
   parameter int BASE = 40;
   typedef enum { LOW, HIGH = 7 } level_t;
   function automatic int plus(int a, int b);
      return a + b;
   endfunction
endpackage

module svimport1;
   import imppkg::*;
   import imppkg::BASE;

   level_t l;

   initial begin
      l = HIGH;
      if (plus(BASE, 2) !== 42 || l !== 7)
        $display("FAILED");
      else
        $display("PASSED");
   end
endmodule
