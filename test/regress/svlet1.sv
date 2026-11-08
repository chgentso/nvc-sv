// let declarations with and without arguments
// Expected values: IEEE 1800-2017 11.12 (let: formal arguments replaced by the actuals, which keep their
// own precedence as if parenthesized; default argument values; named/positional binding; formals hide
// module variables of the same name)

module svlet1;
  logic [3:0] a = 12;
  logic [3:0] b = 15;
  logic [3:0] c = 7;
  logic d;
  int x = 5, y = 6;
  int errs = 0;
  let op(x, y, z) = |((x | y) & z);
  let add(p, q = 10) = p + q;
  let max2(m, n) = (m > n) ? m : n;
  let konst = 42;
  let sq(v) = v * v;
  let nolist = x + y;
  initial begin
    d = op(.x(a), .y(b), .z(c));
    if (d !== 1'b1) begin errs++; $error("A d=%b", d); end
    if (add(1, 2) !== 3) begin errs++; $error("B1"); end
    if (add(5) !== 15) begin errs++; $error("B2"); end
    if (add(.q(1), .p(2)) !== 3) begin errs++; $error("B3"); end
    if (max2(x, y) !== 6) begin errs++; $error("C1"); end
    if (max2(y + 1, x * 2) !== 10) begin errs++; $error("C2"); end
    if (sq(3 + 1) !== 16) begin errs++; $error("D %0d", sq(3 + 1)); end
    if (sq(add(1, 1)) !== 4) begin errs++; $error("E"); end
    if (op(4'b0001, 4'b0000, 4'b0001) !== 1'b1) begin errs++; $error("F"); end
    if (op(4'b0001, 4'b0000, 4'b0010) !== 1'b0) begin errs++; $error("G"); end
    if (konst !== 42 || nolist !== 11) begin errs++; $error("H"); end
    if (errs == 0) $display("PASSED");
  end
endmodule
