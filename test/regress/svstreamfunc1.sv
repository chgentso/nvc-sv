// Expected values: IEEE 1800-2017 11.4.14 (streaming: {<<4{x}} reverses nibbles, {<<{x}} reverses bits),
// 13.4 (functions in a package), 9.2.2.2 (always_comb is evaluated at time zero and when its inputs change)
`timescale 1ns/1ns
package p;
  function automatic logic [7:0] rev4(logic [7:0] x);
    return {<<4{x}};
  endfunction
  function automatic logic [7:0] revb(logic [7:0] x);
    logic [7:0] r;
    r = {<<{x}};
    return r;
  endfunction
endpackage
module svstreamfunc1;
  logic [7:0] a = 8'h1c;
  logic [7:0] b, c;
  int errs = 0;
  always_comb b = p::rev4(a);
  always_comb c = p::revb(a);
  initial begin
    #1;
    // 1c -> nibbles swapped c1; bits 00011100 reversed 00111000 = 38
    if (b !== 8'hc1 || c !== 8'h38) begin $error("a=1c: b=%h c=%h expected c1 38", b, c); errs++; end
    a = 8'h35; #1;
    // 35 -> 53; 00110101 reversed 10101100 = ac
    if (b !== 8'h53 || c !== 8'hac) begin $error("a=35: b=%h c=%h expected 53 ac", b, c); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
