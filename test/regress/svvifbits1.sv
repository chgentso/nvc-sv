// Self-checking. Expected values: IEEE 1800-2017 6.22.1 (matching types: two
// virtual interface types with the same interface and equal parameter values
// are the same type however the values are written), 8.25 (each
// specialisation of a class is one class with one copy of its static members)
// and 20.6.2 ($bits of a type is its width, here a constant used in a
// parameter value).
interface svvifbits1_if #(parameter int W = 1) (input wire [W-1:0] p);
endinterface

package svvifbits1_pkg;
  typedef enum logic [3:0] { A = 4'h5, B = 4'ha } e_t;

  class store #(type T = int);
    static T val;
  endclass
endpackage

module svvifbits1_get(output bit found);
  import svvifbits1_pkg::*;
  initial begin
    #1;
    found = store #(virtual svvifbits1_if #($bits(svvifbits1_pkg::e_t) +1 ))::val != null;
  end
endmodule

module svvifbits1;
  import svvifbits1_pkg::*;
  wire [4:0] w;
  svvifbits1_if #($bits(w)) i(w);
  bit found;
  int errs = 0;
  svvifbits1_get g(.found(found));
  virtual svvifbits1_if #(5) lit;
  initial begin
    store #(virtual svvifbits1_if #($bits(e_t) + 1))::val = i;
    #2;
    if (!found) begin errs++; $error("specialisation differs between scopes"); end
    lit = store #(virtual svvifbits1_if #(5))::val;
    if (lit == null) begin errs++; $error("literal specialisation differs"); end
    if (errs == 0) $display("PASSED");
  end
endmodule
