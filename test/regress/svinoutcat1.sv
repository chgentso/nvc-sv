// Expected values: IEEE 1800-2017 23.3.3.? (inout port connected to a concatenation of nets: p[3]=a, p[2]=b,
// p[1:0]=c, all the same nets), 6.6.1 (wire resolution: z loses, equal values merge, conflicting values give x),
// 10.3.2 (continuous assignment drivers), 9.4.2 (@ on a net). Changes of 'seen' are collected per time step;
// values before time 1 (while en was still unknown) are not checked.
`timescale 1ns/1ns
module child(inout [3:0] p, input en, input [3:0] v);
  assign p = en ? v : 4'bzzzz;
  wire [3:0] seen = p;
  string log[$];
  always @(seen) if ($time > 0) log.push_back($sformatf("%0d %b", $time, seen));
endmodule
module svinoutcat1;
  wire a, b;
  wire [1:0] c;
  reg en;
  reg [3:0] v;
  reg pa, pb;
  reg [1:0] pc;
  int err = 0;
  assign a = pa ? 1'b1 : 1'bz;
  assign c = pc[0] ? 2'b10 : 2'bzz;
  child d(.p({a, b, c}), .en(en), .v(v));
  `define CHK(exp) if ({a, b, c} !== exp) begin err++; $error("t=%0t a=%b b=%b c=%b expected %b", $time, a, b, c, exp); end
  initial begin
    en = 0; v = 4'b0101; pa = 0; pc = 0;
    #1 `CHK(4'bzzzz)
    pa = 1;
    #1 `CHK(4'b1zzz)
    pa = 0; en = 1;
    #1 `CHK(4'b0101)
    pc[0] = 1;
    #1 `CHK(4'b01xx)
    en = 0; pa = 1;
    #1 `CHK(4'b1z10)
    pa = 0; pc[0] = 0;
    #1 `CHK(4'bzzzz)
    begin
      string exp[$];
      exp = '{"1 1zzz", "2 0101", "3 01xx", "4 1z10", "5 zzzz"};
      if (d.log.size() != exp.size()) begin err++; $error("child saw %0d changes, expected %0d", d.log.size(), exp.size()); end
      else foreach (exp[k]) if (d.log[k] != exp[k]) begin err++; $error("child change %0d: '%s' expected '%s'", k, d.log[k], exp[k]); end
    end
    if (err == 0) $display("PASSED");
    $finish;
  end
endmodule
