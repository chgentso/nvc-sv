module covbin1;
   bit [3:0] v;
   bit gate;

   covergroup cg;
      cp1: coverpoint v {
         bins a = {[0:3]};
         bins a = {[4:7]};           // Error: duplicate bin name
      }
      cp2: coverpoint v {
         bins a = {[0:3]};
         ignore_bins a = {[4:7]};    // Error: duplicate across kinds
      }
      cp3: coverpoint v {
         bins a = {[0:3]};           // OK: different coverpoint
         ignore_bins d = default;    // Error: ignore_bins default
      }
      cp4: coverpoint v {
         bins low = {0};
         ignore_bins ds = default sequence iff (gate); // Error
      }
   endgroup
endmodule
