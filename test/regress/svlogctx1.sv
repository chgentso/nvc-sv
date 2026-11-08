// Expected values: IEEE 1800-2017 11.4.7 (logical operators: result is 1-bit, x/0/1 rules; operands
// are self-determined and "true" means nonzero), 12.4 (if on an x condition takes the else branch),
// 11.4.11 (conditional operator), 11.4.8 (bitwise negation of a 1-bit operand)
module svlogctx1;
  bit r = 1;
  int z = 0, nz = 5;
  logic [3:0] w = 4'b0001;
  logic x = 1'bx;
  logic [63:0] big = 64'h1;
  bit signed [7:0] sg = -1;
  int errs = 0;
  logic [2:0] res;
  initial begin
    if ((~r) || z) begin errs++; $error("A"); end
    if (z || (~r)) begin errs++; $error("B"); end
    if (((~r) && 1) || z) begin errs++; $error("C"); end
    if ((~w[0]) && nz) begin errs++; $error("D"); end
    if ((~big[0]) || (~r)) begin errs++; $error("E"); end
    if (x || z) begin errs++; $error("F"); end
    res = {(~r) || z, x || nz, x && z};
    if (res !== 3'b010) begin errs++; $error("res=%b", res); end
    if (!(sg && nz)) begin errs++; $error("G"); end
    if ((r ? 1'b0 : 4'hf) || z) begin errs++; $error("H"); end
    if (((~r) || (~r) || nz) !== 1'b1) begin errs++; $error("last"); end
    if (errs == 0) $display("PASSED");
  end
endmodule
