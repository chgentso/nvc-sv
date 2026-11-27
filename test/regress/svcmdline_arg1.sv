package svcmdline_arg1_pkg;
   function string uvm_dpi_get_next_arg(int init = 0);
      return "";
   endfunction
endpackage

module svcmdline_arg1;
   import svcmdline_arg1_pkg::*;

   initial begin
      string s;
      int doInit = 1;
      int n = 0;
      int saw_foo = 0;
      do begin
         s = uvm_dpi_get_next_arg(doInit);
         doInit = 0;
         if (s != "") begin
            n++;
            if (s == "+FOO=bar")
               saw_foo = 1;
            if (n > 100)
               $fatal(1, "infinite loop in uvm_dpi_get_next_arg: %s", s);
         end
      end while (s != "");
      if (!saw_foo)
         $fatal(1, "missing +FOO=bar arg");
      $display("OK n=%0d", n);
   end
endmodule
