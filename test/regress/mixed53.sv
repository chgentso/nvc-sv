`timescale 1ns/1ns
// Hierarchical reads of fields of VHDL record signals from Verilog, through
// generate-indexed names (u.g[0].l ...), for a signal declared in a generate block,
// in a nested instance, and a port-mapped array element.
// Expected values: IEEE 1800-2017 23.6 (hierarchical names through generate
// blocks), 6.9.1 (4-state vectors; std_logic '0'/'1' map to 0/1).  From
// mixed53.vhd: gs = ('0', "1010") in each generate block; the leaf in block i has
// id = i + 3 and drives rp = ('1', to_unsigned(id, 4)); its local loc = ('1', "0001");
// outs(i) is connected to rp, so outs(0).mask = 0011, outs(1).mask = 0100.
module mixed53;
  mixed53_e u();
  int errors = 0;
  initial begin
    #1;
    if (u.g[0].gs.valid !== 1'b0 || u.g[1].gs.mask !== 4'b1010) begin
      $error("a: %b %b, expected 0 1010", u.g[0].gs.valid, u.g[1].gs.mask); errors++; end
    if (u.g[0].l.loc.mask !== 4'b0001 || u.g[1].l.loc.valid !== 1'b1) begin
      $error("b: %b %b, expected 0001 1", u.g[0].l.loc.mask, u.g[1].l.loc.valid); errors++; end
    if (u.outs[0].mask !== 4'b0011 || u.outs[1].mask !== 4'b0100) begin
      $error("c: %b %b, expected 0011 0100", u.outs[0].mask, u.outs[1].mask); errors++; end
    if (u.g[0].l.rp.mask !== 4'b0011 || u.g[1].l.rp.mask !== 4'b0100) begin
      $error("d: %b %b, expected 0011 0100", u.g[0].l.rp.mask, u.g[1].l.rp.mask); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
