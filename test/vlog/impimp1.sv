package base_pkg;
  class base_cfg;
  endclass
endpackage

package lib_pkg;
  import base_pkg::*;
endpackage

package top_pkg;
  import lib_pkg::*;
  class agent_cfg extends base_cfg;   // Error: not exported
  endclass
endpackage
