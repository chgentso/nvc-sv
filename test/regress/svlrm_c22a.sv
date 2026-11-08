// Self-checking test. Expected values: IEEE 1800-2017 22.5.1 (`define with arguments, `" stringify,
// `` token pasting, backslash-newline continuation, nested macro calls), 22.5.2 (`undef), 22.6
// (`ifdef, `ifndef, `else, `elsif, `endif), 22.7 (`timescale), 22.8 (`default_nettype), 22.9
// (`resetall), 22.13 (`__LINE__ expands to the current line number: two consecutive lines differ
// by one). Macro text containing a begin/end pair is legal (22.5.1).
// `" is only legal inside macro text (22.5.1), so it is not used elsewhere.
`define ADD(a, b) ((a) + (b))
`define STR(x) `"x`"
`define CAT(a, b) a``b
`define VAL 5
`define EMPTY
`define MULTI(a) \
  a + 1
`ifdef VAL
  `define HAS_VAL 1
`else
  `define HAS_VAL 0
`endif
`ifndef NOT_DEFINED
  `define ND 1
`endif
`ifdef NOT_DEFINED
  `define E1 1
`elsif VAL
  `define E1 2
`else
  `define E1 3
`endif
`undef EMPTY
`ifdef EMPTY
  `define EE 1
`else
  `define EE 0
`endif
`timescale 1ns/1ps
`default_nettype none
module svlrm_c22a;
  int x, y_1;
  int errs, l1, l2;
  bit blk_ran;
  function automatic void ck(bit ok, string m);
    if (!ok) begin errs++; $error("%s", m); end
  endfunction
  initial begin
    x = `ADD(1, 2) * 2; ck(x == 6, "ADD");
    ck(`STR(hello world) == "hello world", "STR");
    ck(`VAL == 5 && `HAS_VAL == 1 && `ND == 1 && `E1 == 2, "conditional compilation");
    ck(`EE == 0, "undef");
    `CAT(y, _1) = 7; ck(y_1 == 7, "CAT");
    ck(`MULTI(3) == 4, "MULTI continuation line");
    l1 = `__LINE__;
    l2 = `__LINE__;
    ck(l1 > 0 && l2 == l1 + 1, "__LINE__ on consecutive lines");
    ck(`ADD(`ADD(1, 2), `VAL) == 8, "nested macro call");
    #1 ck(blk_ran, "macro-expanded begin/end block ran");
    if (errs == 0) $display("PASSED");
  end
`define BEGIN_BLK begin
`define END_BLK end
  initial `BEGIN_BLK blk_ran = 1; `END_BLK
endmodule
`resetall
