// IEEE 1800-2017 25.9: a virtual interface declared with a modport has the
// access restrictions of that modport, so as for a module port of that
// modport type (25.5) an item listed as an input cannot be assigned through
// it; items listed as outputs or inouts can, and every listed item can be
// read.  A virtual interface without a modport is not restricted.
interface vifmodportin1_if;
  logic a, b, c;
  logic [3:0] v;
  modport mp(input a, v, output b, inout c);
endinterface

class vifmodportin1_cls;
  virtual vifmodportin1_if.mp vif;
  function void f();
    vif.a = 1;                          // Error
    vif.b = vif.a;                      // OK
  endfunction
endclass

module vifmodportin1;
  virtual vifmodportin1_if.mp v;
  virtual vifmodportin1_if w;
  vifmodportin1_cls obj;

  task t(virtual vifmodportin1_if.mp p);
    p.v[1] = 0;                         // Error
    p.c = p.v[0];                       // OK
  endtask

  initial begin
    v.a = 1;                            // Error
    v.a <= 0;                           // Error
    v.v[2] = 1;                         // Error
    v.v[3:2] <= 2'b01;                  // Error
    {v.b, v.a} = 2'b10;                 // Error
    obj.vif.a = 0;                      // Error
    v.b = v.a;                          // OK
    v.b <= v.v[1];                      // OK
    v.c = 1;                            // OK
    w.a = 1;                            // OK
    obj.vif.b = obj.vif.a;              // OK
  end
endmodule
