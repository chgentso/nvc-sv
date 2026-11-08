// A parameter with the value $ and $isunbounded
// Expected values: IEEE 1800-2017 6.20.1 (unbounded parameter value $), 20.6.1 ($isunbounded).
// The numeric value of an unbounded parameter is not defined and is not printed or compared.

module svisunbounded1_ch #(parameter int P = 3, parameter int Q = $) ();
  bit UP, UQ, U5;
  initial begin
    UP = $isunbounded(P);
    UQ = $isunbounded(Q);
    U5 = $isunbounded(5);
  end
endmodule

module svisunbounded1;
  parameter int i = $;
  parameter int j = 4;
  localparam int k = $;
  localparam int m = j;
  int errs = 0;
  svisunbounded1_ch u1 ();
  svisunbounded1_ch #(.P($), .Q(7)) u2 ();
  initial begin
    #1;
    if (u1.UP !== 1'b0 || u1.UQ !== 1'b1 || u1.U5 !== 1'b0) begin errs++; $error("u1 %b %b %b", u1.UP, u1.UQ, u1.U5); end
    if (u2.UP !== 1'b1 || u2.UQ !== 1'b0 || u2.U5 !== 1'b0) begin errs++; $error("u2 %b %b %b", u2.UP, u2.UQ, u2.U5); end
    if ($isunbounded(i) !== 1) begin errs++; $error("i"); end
    if ($isunbounded(j) !== 0) begin errs++; $error("j"); end
    if ($isunbounded(k) !== 1) begin errs++; $error("k"); end
    if ($isunbounded(m) !== 0) begin errs++; $error("m"); end
    if ($isunbounded(j + 1) !== 0) begin errs++; $error("j+1"); end
    if (errs == 0) $display("PASSED");
  end
endmodule
