// IEEE 1800-2017 25.9 (virtual interfaces) and 3.12.1 (compilation units):
// a class may name an interface declared later in the same file.  The file
// is analysed twice into the same library: the second analysis must use
// the interface from that analysis and not the copy left by the first.
class drv;
  virtual svreanal1_if v;
  virtual svreanal1_if.mon m;
  task run();
    @(posedge v.clk);
    v.data = 8'h5a;
    @(posedge m.clk);
    if (m.data !== 8'h5a) $error("data %h", m.data);
    else $display("PASSED");
  endtask
endclass

interface svreanal1_if(input logic clk);
  logic [7:0] data;
  modport mon(input data, input clk);
endinterface

module svreanal1;
  logic clk = 0;
  always #5 clk = ~clk;
  svreanal1_if b(clk);
  initial begin
    automatic drv d = new;
    d.v = b;
    d.m = b;
    d.run();
    $finish;
  end
endmodule
