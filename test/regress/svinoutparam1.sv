// Expected values: IEEE 1800-2017 25.5 (modport inout of an interface net), 23.3.3 (inout ports are the same net
// on both sides, with the parameterised widths 8), 6.6.1 (z loses to a driven value), 10.3 (assign): with
// dir = 0, buf1 drives b (= D) from a (= i.data = 8'h5a) and drives z on a.
`timescale 1ns/1ns
interface ifc #(parameter w = 32) ();
  wire [w-1:0] data;
  modport slave(inout data);
endinterface
module buf1 #(parameter w = 1) (inout [w-1:0] a, b, input dir);
  assign b = !dir ? a : 'bz;
  assign a = dir ? b : 'bz;
endmodule
module sub #(parameter w = 32) (inout [w-1:0] D, ifc.slave slave);
  buf1 #(w) b2 (slave.data, D, 1'b0);
endmodule
module svinoutparam1;
  ifc #(8) i();
  wire [7:0] D;
  sub #(8) u(D, i);
  assign i.data = 8'h5a;
  initial #1 begin
    if (D !== 8'h5a) $error("D=%h expected 5a", D);
    else if (i.data !== 8'h5a) $error("i.data=%h expected 5a", i.data);
    else $display("PASSED");
  end
endmodule
