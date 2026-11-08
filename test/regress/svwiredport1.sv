// A wired net declared for a port is also the type of the net connected to it
// Expected values: IEEE 1800-2017 23.3.3 (a net connected through a port collapses with the
// port net), 6.6.2 (triand resolves drivers by AND, wor by OR): d = dd & 1 = 0 and
// e = dd | 1 = 1.
`timescale 1ns/1ns
module svwiredport1_sub(D, E);
  inout D;
  inout E;
  reg dd = 0;
  triand D = dd;
  wor E = dd;
  assign D = 1'b1;
  assign E = 1'b1;
endmodule

module svwiredport1;
  wire d, e;
  svwiredport1_sub u(.D(d), .E(e));
  initial begin
    #1;
    if ({d, e} !== 2'b01) $error("d=%b e=%b exp 0 1", d, e);
    else $display("PASSED");
  end
endmodule
