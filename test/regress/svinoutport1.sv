// Expected values: IEEE 1800-2017 23.3.3 (inout ports are collapsed nets), 25.? (net inside an interface port),
// 6.6.1 (wire resolution: z loses; nets with only z drivers are z, so a loop of high-impedance drivers stays z),
// 10.3 (assign). The pad module does not connect its port p to w, so pd.w has only z drivers.
`timescale 1ns/1ns
interface svinoutport1_if;
  wire [3:0] a;
  wire b;
endinterface
module svinoutport1_buf #(parameter N = 1) (inout [N-1:0] x, inout [N-1:0] y, input dir);
  assign x = dir ? y : 'z;
  assign y = dir ? 'z : x;
endmodule
module svinoutport1_pad(inout [3:0] p);
  wire [3:0] w;
endmodule
module svinoutport1_m (svinoutport1_if slave, inout [3:0] pins, input dir);
  svinoutport1_buf #(4) b0 (slave.a, pins, dir);
endmodule
module svinoutport1;
  svinoutport1_if i();
  wire [3:0] pins;
  reg d = 1;
  reg [3:0] ext = 4'bz;
  int err = 0;
  assign pins = ext;
  svinoutport1_m u (i, pins, d);
  svinoutport1_pad pd(pins);
  svinoutport1_buf #(4) b1 (pd.w, pins, 1'b0);
  initial begin
    ext = 4'b1010; #1;
    if (i.a !== 4'b1010 || pd.w !== 4'bzzzz) begin err++; $error("a=%b w=%b", i.a, pd.w); end
    ext = 4'bzzzz; d = 0; #1;
    if (i.a !== 4'bzzzz || pins !== 4'bzzzz || pd.w !== 4'bzzzz) begin err++; $error("a=%b pins=%b w=%b", i.a, pins, pd.w); end
    if (err == 0) $display("PASSED");
  end
endmodule
