// Inout port connected to an interface member of a different width
// Expected values: IEEE 1800-2017 23.3.3 (port connections), 10.3 (continuous assignment),
// 6.5 / Table 6-2 (net resolution)
`timescale 1ns/1ns
interface ifc;
  wire [31:1] addr;
  wire [31:0] data;
  wire lword_n;
  modport slave(inout addr, inout data, inout lword_n);
endinterface
module buf1 #(parameter w = 1) (inout [w-1:0] a, b, input dir, oe_n);
  assign b = (!dir && !oe_n) ? a : 'bz;
  assign a = (dir && !oe_n) ? b : 'bz;
endmodule
module sub(inout [31:1] A, inout [31:0] D, inout LW, input d1, input d2, ifc.slave slave);
  buf1 #(1) b0 (slave.lword_n, LW, d1, d2);
  buf1 #(31) b1 (slave.addr, A, d1, d2);
  buf1 #(33) b2 (slave.data, D, d1, d2);
endmodule
module svinoutwidth2;
  ifc i();
  wire [31:1] A; wire [31:0] D; wire LW;
  reg dir = 0;
  reg [31:0] dv = 32'h12345678;
  int errs = 0;
  assign D = dir ? dv : 32'bz;
  sub u(A, D, LW, dir, 1'b0, i);
  assign i.data = dir ? 32'bz : 32'h0f0f0f0f;
  initial begin
    #1;
    if (D !== 32'h0f0f0f0f || i.data !== 32'h0f0f0f0f) begin errs++; $error("t=1 D=%h i.data=%h", D, i.data); end
    dir = 1;
    #1;
    if (D !== 32'h12345678 || i.data !== 32'h12345678) begin errs++; $error("t=2 D=%h i.data=%h", D, i.data); end
    dir = 0; dv = 32'hdeadbeef;
    #1;
    if (D !== 32'h0f0f0f0f || i.data !== 32'h0f0f0f0f) begin errs++; $error("t=3 D=%h i.data=%h", D, i.data); end
    if (errs == 0) $display("PASSED");
  end
endmodule
