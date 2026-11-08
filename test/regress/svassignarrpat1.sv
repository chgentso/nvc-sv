// Self-checking. Expected values: IEEE 1800-2017 10.3 (continuous assignment to an unpacked array variable
// re-evaluates when an operand changes), 10.9.1 (array assignment patterns, positional), 11.4.12 (concatenation
// {hi,lo}), 11.5.1 (part select q[7:0]).
`timescale 1ns/1ns
`define CHK(tag, got, exp) if ((got) !== (exp)) begin errs++; $error("%s: got %0h expected %0h", tag, got, exp); end
module svassignarrpat1;
  int errs = 0;
  localparam int N = 1;
  logic [11:0] q = 12'h123;
  logic [31:0] hi = 32'h11, lo = 32'h22;
  logic [11:0] pre[N];
  logic [63:0] cmp[N][N];
  logic [7:0] two[2];
  assign pre = '{q};
  assign cmp = '{'{{hi, lo}}};
  assign two = '{q[7:0], 8'h55};
  initial begin
    #1;
    `CHK("pre", pre[0], 12'h123) `CHK("cmp", cmp[0][0], 64'h0000001100000022)
    `CHK("two0", two[0], 8'h23) `CHK("two1", two[1], 8'h55)
    hi = 32'h33; q = 12'h456;
    #1;
    `CHK("pre'", pre[0], 12'h456) `CHK("cmp'", cmp[0][0], 64'h0000003300000022)
    `CHK("two0'", two[0], 8'h56) `CHK("two1'", two[1], 8'h55)
    if (errs == 0) $display("PASSED");
  end
endmodule
