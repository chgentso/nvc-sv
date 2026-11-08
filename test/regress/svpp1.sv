// Macro features the UVM library relies on
`define PASTED_NAME_X(a) a
`define J(T, y) `PASTED_``T``_X(y)
`define OUTER(y) `J(NAME, y)
`define COMMENT_CONT(x) x + \
   // ignored \
   1
`define WITH_DIR(x) \
`ifdef NOT_DEFINED \
   -1 \
`else \
   x \
`endif
`define STR(x) `"x`"
`define INNER 5
`define PASS(x) x
`define VER_MAJOR 1
`define VER_MINOR 2
`define VERSION `"uvm-`VER_MAJOR.`VER_MINOR`"

module svpp1;
  int failed = 0;

  `define check(x, y) \
    if ((x) !== (y)) begin $display("FAILED %s", `"x`"); failed++; end

  initial begin
    `check(`J(NAME, 3), 3);
    `check(`OUTER(4), 4);
    `check(`COMMENT_CONT(2), 3);
    `check(`WITH_DIR(6), 6);
    if (`STR(ab) != "ab") failed++;
    `check(`PASS(`INNER), 5);
    `check(`PASS(
                 7
                 ), 7);    // Argument over several lines
    `check(`__LINE__, 37);
    if (`VERSION != "uvm-1.2") failed++;
    if ({`PASS("x\""), `PASS("y")} != "x\"y") failed++;
    if (failed == 0) $display("PASSED");
    $finish;
  end
endmodule
