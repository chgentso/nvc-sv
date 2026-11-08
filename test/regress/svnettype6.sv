`timescale 1ns/1ns
// Nets of user-defined nettypes declared in an interface and driven through interface ports (generic and with a
// modport) by several modules, and a port of an interface that has a user-defined nettype
// Expected values: IEEE 1800-2017 6.6.7 (one resolution call over all the drivers of the net), 25.3 / 25.4
// (interface items accessed through an interface port are the items of the connected instance), 25.5
// (modports: inout access to a net), 23.3.3.5 (ports of the same nettype merge the nets).
// wnet resolves to the maximum driver value plus 100 times the number of drivers.
package svnettype6_pkg;
  function automatic int res_max (input int d[]);
    int m = -1000;
    foreach (d[i]) if (d[i] > m) m = d[i];
    return m + 100 * d.size();
  endfunction
  nettype int wnet with res_max;
endpackage

interface svnettype6_if import svnettype6_pkg::*; (inout wnet ext);
  wnet w;
  assign w = 1;
  modport drv (inout w);
  modport mon (input w);
endinterface

module svnettype6_a (svnettype6_if bus, input int v);
  assign bus.w = v;
  assign bus.ext = v + 1;
endmodule

module svnettype6_b (svnettype6_if.drv bus, input int v);
  assign bus.w = v;
endmodule

module svnettype6_m (svnettype6_if.mon bus, output int wv);
  assign wv = bus.w;
endmodule

module svnettype6;
  import svnettype6_pkg::*;
  wnet e;
  int x = 5, y = 7;
  int wv, ev;
  int fails = 0;

  svnettype6_if i(.ext(e));
  svnettype6_a a(.bus(i), .v(x));
  svnettype6_b b(.bus(i), .v(y));
  svnettype6_m m(.bus(i), .wv);
  assign ev = i.ext;
  assign e = 0;

  initial begin
    // w: drivers 1 (interface), x (a), y (b); ext/e: x + 1 (a), 0 (here)
    #1 if (i.w != 7 + 300 || wv != i.w || e != 6 + 200 || ev != e || i.ext != e) begin
      $error("t=1: i.w = %0d wv = %0d e = %0d ev = %0d i.ext = %0d", i.w, wv, e, ev, i.ext);
      fails++;
    end
    x = 20; #1;
    if (i.w != 20 + 300 || wv != i.w || e != 21 + 200 || ev != e) begin
      $error("t=2: i.w = %0d wv = %0d e = %0d ev = %0d", i.w, wv, e, ev);
      fails++;
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
