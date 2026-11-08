`timescale 1ns/1ps
package pp;
  class c;
    function void p(); $display("pkg %0t|%t|%0d|%0t", $time, $time, $time, $realtime); endfunction
    function void q(time t); $display("arg %0t|%0d", t, t); endfunction
  endclass
endpackage
module svtimefmt1;
  import pp::*;
  c o = new;
  initial begin
    #300;
    o.p();
    o.q(7);
    o.q(300ns);
    $display("mod %0t|%0d|%0t", $time, $time, $realtime);
  end
endmodule
