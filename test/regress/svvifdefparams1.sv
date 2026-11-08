// Expected values: IEEE 1800-2017 8.25 / 8.9 (static members of a parameterized class are shared by all uses of one
// specialization), 25.9 (virtual interface passed through an output argument), 6.20.2 (default parameter values)
package dbp;
class db #(type T = int);
  static T slot;
  static bit have;
  static function void set(T v); slot = v; have = 1; endfunction
  static function bit get(output T v); v = slot; return have; endfunction
endclass
endpackage
package agp;
  import dbp::*;
  class agcfg;
    virtual ifc vif;
  endclass
  class ag;
    agcfg cfg = new;
    bit ok;
    int n;
    function void build();
      ok = db#(virtual ifc)::get(cfg.vif);
      if (ok) n = cfg.vif.n;
    endfunction
  endclass
endpackage
interface ifc #(parameter int unsigned NumChannels = 2);
  logic [NumChannels-1:0] pda;
  int n = NumChannels;
endinterface
module svvifdefparams1;
  import dbp::*;
  localparam int NUM = 2;
  ifc #(NUM) i();
  initial begin
    automatic agp::ag a = new;
    db#(virtual ifc)::set(i);
    a.build();
    if (!a.ok) $error("no vif found");
    else if (a.n !== 2) $error("n=%0d, expected 2", a.n);
    else $display("PASSED");
  end
endmodule
