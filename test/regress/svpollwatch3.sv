// Polled waits which depend on handle chains, calls, time, associative
// arrays, strings, static variables and four-state signal values: each is
// evaluated again when anything it reads changes (IEEE 1800-2017 9.4.3).

package pw3_pkg;
   int g = 0;
endpackage

interface ifc3;
   logic [3:0] data;
endinterface

module svpollwatch3;
   import pw3_pkg::*;

   ifc3 va(), vb();

   class Cfg;
      int state = 0;
      int other = 0;
      string name = "";
      virtual ifc3 v;
      Cfg next;
      event ev;
      virtual function int get();
         return other;
      endfunction
   endclass

   class Sub extends Cfg;
      virtual function int get();
         return state;
      endfunction
   endclass

   Cfg cfg, cfga, cfgb;
   int aa[int];
   int log_n = 0;

   task automatic note(string s);
      log_n++;
      $display("%0t %s", $time, s);
   endtask

   function automatic int plain(Cfg c);
      return c.other;
   endfunction

   initial begin
      cfga = new; cfgb = new;
      cfga.v = va; cfgb.v = vb;
      va.data = 4'h0; vb.data = 4'h5;
      cfg = cfga;
      cfga.next = new; cfgb.next = new;
      cfgb.next.state = 4;
      cfgb.state = 3;

      fork
         // The handle changes to an object whose member already holds the value
         begin wait (cfg.state == 3); note("handle member"); end
         // The interface handle changes to an instance holding the value
         begin wait (cfg.v.data == 4'h5); note("vif switched"); end
         // A chain of two handles
         begin wait (cfg.next.state == 4); note("chain"); end
         // Four-state values: 0 to x and then x to z
         begin wait (va.data === 4'bxxxx); note("x seen"); end
         begin wait (va.data === 4'bzzzz); note("z seen"); end
         // Function call, virtual method call, time, aa, string, static
         begin wait (plain(cfg) == 7); note("function"); end
         begin wait (cfg.get() == 8); note("virtual"); end
         begin wait (cfgb.state == 3 && $time > 3); note("time"); end
         begin wait (aa.exists(2)); note("exists"); end
         begin wait (aa[3] == 1); note("aa element"); end
         begin wait (cfgb.name == "go"); note("string"); end
         begin wait (g == 5); note("static"); end
         begin wait (cfgb.ev.triggered); note("triggered"); end
      join_none

      #1;
      cfg = cfgb;                // member, chain and vif now hold the values
      #1;
      va.data = 4'bxxxx;
      #1;
      va.data = 4'bzzzz;
      #1;
      cfg.other = 7;
      #1;
      begin
         Sub s = new;
         s.state = 8;
         cfg = s;
      end
      #1 aa[2] = 1;
      #1 aa[3] = 1;
      #1 cfgb.name = "go";
      #1 g = 5;
      #1 -> cfgb.ev;
      #50;
      if (log_n != 13) $fatal(1, "%0d woke", log_n);
      $display("PASSED");
   end
endmodule
