// A specialisation of a class is the same one wherever it is named, also
// when its type parameter is a virtual interface whose parameter is given
// by an expression of other parameters (IEEE 1800-2017 8.25).
interface svspecvif1_if #(parameter int Width = 1) (input logic clk);
  logic [Width-1:0] v;
endinterface

class svspecvif1_db #(type T = int);
  static T slot;
  static bit is_set;
  static function void put(T t); slot = t; is_set = 1; endfunction
  static function bit get(output T t); t = slot; return is_set; endfunction
endclass

package svspecvif1_pkg;
  parameter int unsigned A = 3;
  parameter int unsigned NUM = A * 4 + 2;
  typedef virtual svspecvif1_if #(NUM) vif_t;

  class user;
    function bit fetch(output vif_t v);
      return svspecvif1_db#(vif_t)::get(v);
    endfunction
  endclass
endpackage

module svspecvif1;
  import svspecvif1_pkg::*;
  logic clk = 0;
  svspecvif1_if #(NUM) i(clk);
  initial begin
    automatic user u = new;
    automatic vif_t v;
    svspecvif1_db#(vif_t)::put(i);
    if (!u.fetch(v)) $fatal(1, "not found in the same specialisation");
    if (v.v !== 14'bx) $fatal(1, "wrong interface");
    $display("PASSED");
  end
endmodule
