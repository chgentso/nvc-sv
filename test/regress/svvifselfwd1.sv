// Bit and part selects (including an out-of-range indexed select) through a virtual interface
// handle declared before the interface itself.
// Expected values: IEEE 1800-2017 11.5.1 (bit-select, indexed part-select, out-of-range bits read x),
// 6.11/11.8 (assigning x to a 2-state bit vector gives 0), 25.9.
`timescale 1ns/1ns
package pk;
  class agcfg;
    virtual ifc vif;
  endclass
  class c;
    agcfg cfg = new;
    int errors;
    task run();
      bit b;
      bit [1:0] w;
      // bus = 4'b1010, arr[0] = 4'h5, arr[1] = 4'h6
      bit       eb [4] = '{0, 1, 0, 1};
      bit [1:0] ew [4] = '{2'b10, 2'b01, 2'b10, 2'b01}; // i=3: bus[4] is out of range -> x -> 0 in a bit vector
      bit       ea [4] = '{1, 1, 1, 0};
      for (int i = 0; i < 4; i++) begin
        b = cfg.vif.bus[i];
        w = cfg.vif.bus[i +: 2];
        if (b !== eb[i] || w !== ew[i] || cfg.vif.arr[i % 2][i] !== ea[i]) begin
          $error("i=%0d b=%b w=%b a=%b", i, b, w, cfg.vif.arr[i % 2][i]); errors++;
        end
      end
      // the 4-state view of the out-of-range select
      if ((cfg.vif.bus[3 +: 2]) !== 2'bx1) begin $error("bus[3+:2]=%b", cfg.vif.bus[3 +: 2]); errors++; end
    endtask
  endclass
endpackage

interface ifc #(parameter int W = 4);
  logic [W-1:0] bus = 4'b1010;
  logic [3:0] arr [2];
  initial begin arr[0] = 4'h5; arr[1] = 4'h6; end
endinterface

module svvifselfwd1;
  ifc #(4) i();
  pk::c o = new;
  initial begin
    o.cfg.vif = i;
    #1 o.run();
    if (o.errors == 0) $display("PASSED");
  end
endmodule
