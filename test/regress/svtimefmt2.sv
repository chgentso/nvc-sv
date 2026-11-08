`timescale 1ns/1ps
package pp;
  class c;
    function void p();
      string s, s2;
      $swrite(s, "%0t", $time);
      s2 = $sformatf("%0t", $time);
      $display("pkg [%s] [%s]", s, s2);
    endfunction
  endclass
endpackage
module svtimefmt2;
  import pp::*;
  c o = new;
  string s;
  initial begin
    #300;
    o.p();
    $swrite(s, "%0t", $time);
    $display("mod [%s]", s);
  end
endmodule
