// let declarations used without parentheses
// Expected values: IEEE 1800-2017 11.12 (let without formals may be used without parentheses; the body is
// evaluated in the scope of the declaration with current variable values), 6.11 (int is signed)

module svlet2;
  int x = 5, y = 6;
  int errs = 0;
  let konst = 42;
  let nolist = x + y;
  let withp() = x - y;
  initial begin
    if (konst !== 42) begin errs++; $error("konst"); end
    if (nolist !== 11) begin errs++; $error("nolist %0d", nolist); end
    if (withp() !== -1) begin errs++; $error("withp"); end
    x = 10;
    if (nolist !== 16) begin errs++; $error("nolist2 %0d", nolist); end
    if (konst + 1 !== 43) begin errs++; $error("konst+1"); end
    if (errs == 0) $display("PASSED");
  end
endmodule
