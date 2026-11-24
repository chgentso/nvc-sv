module dpiname1;
   import "DPI-C" function int good_fn(int i);
   import "DPI-C" good_c = function int \escaped.fn (int i);
   import "DPI-C" function int \bad.import (int i);

   export "DPI-C" function \bad.export ;
   function int \bad.export ();
      return 0;
   endfunction

   export "DPI-C" ok_c = function \ok.export ;
   function int \ok.export ();
      return 1;
   endfunction
endmodule
