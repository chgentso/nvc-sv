// With --relaxed a name imported by a package from another package is found
// through the wildcard imports of the packages, with a warning
package base_pkg;
  class base_cfg;
    int v = 7;
  endclass
  typedef enum { RED, GREEN } color_t;
endpackage
package lib_pkg;
  import base_pkg::*;
endpackage
package top_pkg;
  import lib_pkg::*;
  class agent_cfg extends base_cfg;
  endclass
  class holder;
    base_cfg c;
    color_t col = GREEN;
  endclass
endpackage
module svimpimp1;
  import top_pkg::*;
  initial begin
    holder h;
    h = new;
    h.c = new;
    if (h.c.v == 7 && h.col == base_pkg::GREEN) $display("PASSED");
    else $display("FAILED");
  end
endmodule
