// foreach over members of a virtual interface reached through class handles
// Expected values: IEEE 1800-2017 12.7.3 (foreach: dimensions visited left bound to right bound, the loop
// variable of the inner foreach hides the for variable), 25.9 (virtual interfaces), 20.7 (array query functions
// $left, $right, $size, $dimensions, $low, $high with a dimension argument; dimension 1 is the unpacked [2:3]
// and dimension 2 the packed [1:0]), 7.4.5 (multidimensional arrays), 21.2.1 (%b, %h).
// sio = 4'b1010 gives bits 3..0 = 1,0,1,0; mem[2] = 2'b10, mem[3] = 2'b01.
package pk;
  parameter int W = 4;
  string lg = "";

  class cfg_c;
    virtual sif vif;
  endclass

  class mon;
    cfg_c cfg;

    task run();
      int n = 0;
      for (int i = 0; i < 2; i++) begin
        foreach (cfg.vif.sio[i]) begin
          lg = {lg, $sformatf("i=%0d outer=%0d v=%b\n", i, n, cfg.vif.sio[i])};
          n++;
        end
      end
      foreach (cfg.vif.mem[a, b])
        lg = {lg, $sformatf("mem[%0d][%0d] = %b\n", a, b, cfg.vif.mem[a][b])};
      foreach (cfg.vif.mem[a])
        lg = {lg, $sformatf("mem[%0d] = %h\n", a, cfg.vif.mem[a])};
      lg = {lg, $sformatf("left=%0d right=%0d size=%0d\n", $left(cfg.vif.sio),
               $right(cfg.vif.sio), $size(cfg.vif.sio))};
      lg = {lg, $sformatf("dims=%0d low=%0d high=%0d\n", $dimensions(cfg.vif.mem),
               $low(cfg.vif.mem, 2), $high(cfg.vif.mem, 1))};
    endtask
  endclass
endpackage

interface sif import pk::*; (input rst_n, inout wire [W-1:0] sio);
  logic [1:0] mem [2:3];
  initial begin
    mem[2] = 2'b10;
    mem[3] = 2'b01;
  end
endinterface

module svforeachvif1;
  wire [3:0] sio = 4'b1010;
  sif s(.rst_n(1'b1), .sio(sio));
  localparam string EXP =
    "i=3 outer=0 v=1\ni=2 outer=1 v=0\ni=1 outer=2 v=1\ni=0 outer=3 v=0\n" ;
  localparam string EXP2 =
    "i=3 outer=4 v=1\ni=2 outer=5 v=0\ni=1 outer=6 v=1\ni=0 outer=7 v=0\n" ;
  localparam string EXP3 =
    "mem[2][1] = 1\nmem[2][0] = 0\nmem[3][1] = 0\nmem[3][0] = 1\n" ;
  localparam string EXP4 =
    "mem[2] = 2\nmem[3] = 1\nleft=3 right=0 size=4\ndims=2 low=0 high=3\n";

  initial begin
    automatic pk::mon m = new;
    m.cfg = new;
    m.cfg.vif = s;
    #1;
    m.run();
    $write("%s", pk::lg);
    if (pk::lg != {EXP, EXP2, EXP3, EXP4}) $error("log mismatch:\n%s", pk::lg);
    else $display("PASSED");
  end
endmodule
