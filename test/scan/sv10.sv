interface unk_if (input clk);
  import unk_pkg::*;
  unk_t   drv;
  wire    mon;
  clocking cb @(posedge clk);
    output drv;
    input  mon;
  endclocking
endinterface
