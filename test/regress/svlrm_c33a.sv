module svlrm_c33a;
  // Self-checking test. Expected values: IEEE 1800-2017 11.12 (let construct): formal arguments bind
  // to the actual expressions of the call site (so a local `i` at the call site is used, not the
  // one visible at the let declaration), 11.12.1 (default argument values; an untyped formal takes
  // the self-determined type of its actual, so 8'hf0 + 8'h20 stays 8 bits wide), 11.6
  // (system-task arguments are self-determined; `v + 1` with an int literal is 32 bits wide),
  // a let without arguments reads its free variables at the point of use.
  let add(a, b) = a + b;
  let sq(x) = x * x;
  let defaulted(x, y = 10) = x + y;
  let cond(c, a, b) = c ? a : b;
  int i = 3;
  logic [7:0] v = 8'h0f;
  let vv = v + 1;
  int errs;
  function automatic void ck(bit ok, string m);
    if (!ok) begin errs++; $error("%s", m); end
  endfunction
  initial begin
    ck(add(1, 2) == 3 && sq(5) == 25, "add/sq");
    ck(defaulted(1) == 11 && defaulted(1, 2) == 3, "default argument");
    ck(cond(i > 2, 10, 20) == 10, "cond");
    ck(add(sq(2), sq(3)) == 13, "nested let");
    ck($sformatf("%0d", vv) == "16", "let without arguments reads v");
    v = 8'hff; ck($sformatf("%0d", vv) == "256", "let re-evaluated at use, 32-bit context");
    ck(add(i, i) == 6, "args from caller");
    begin
      automatic int i = 100;
      ck(add(i, 1) == 101, "local i at the call site");
    end
    ck(sq(add(1, 1)) == 4, "sq(add)");
    ck($sformatf("%h", add(8'hf0, 8'h20)) == "10", "untyped args keep 8-bit width");
    if (errs == 0) $display("PASSED");
  end
endmodule
