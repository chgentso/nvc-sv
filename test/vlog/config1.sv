// IEEE 1800-2017 33.4.1: illegal configuration declarations.
module config1_top;
endmodule

config cfg_ok;                          // OK
  design config1_top;
  default liblist work;
  instance config1_top.u1 use config1_top;
endconfig

config cfg_nodesign;
  default liblist work;                 // Error: design statement first
endconfig

config cfg_twodefault;
  design config1_top;
  default liblist work;
  default liblist work;                 // Error: at most one default clause
endconfig

config cfg_badpath;
  design config1_top;
  instance other.u1 use config1_top;    // Error: not a design top cell
endconfig

config cfg_param;
  parameter P = 1;                      // Error: only localparam
  design config1_top;
endconfig

config cfg_label;
  design config1_top;
endconfig : cfg_wrong                   // Error: label mismatch
