// Self-checking test. Expected values: IEEE 1800-2017 29.3 (combinational UDPs: table lookup on the
// current inputs; x inputs give the table's x entries), 29.4 (sequential UDPs: level-sensitive latch,
// edge-sensitive flip-flop with (01) entries, `-` no change, `*` any change, `?` any of 0 1 x),
// 29.5 (an input change or state with no matching table entry gives x), 29.7 (initial
// statement of a sequential UDP). Stimulus steps are separated by #1.
// The first sample (all inputs change in the same time step from x) is only checked for the
// combinational outputs; the reaction of a sequential UDP to simultaneous input changes is not specified.
// The counter edge_cnt has no entry for a change on rst from 1 to 0: its output becomes x and stays x
// until rst=1 resets it (entry `? 1 : ? : 0`).
primitive and2 (output y, input a, b);
  table
    0 0 : 0;
    0 1 : 0;
    1 0 : 0;
    1 1 : 1;
    x 0 : 0;
    0 x : 0;
    x 1 : x;
    1 x : x;
    x x : x;
  endtable
endprimitive
primitive mux (output y, input s, a, b);
  table
    0 0 ? : 0;
    0 1 ? : 1;
    1 ? 0 : 0;
    1 ? 1 : 1;
    ? 0 0 : 0;
    ? 1 1 : 1;
  endtable
endprimitive
primitive dff (output reg q, input d, clk);
  initial q = 0;
  table
    0 (01) : ? : 0;
    1 (01) : ? : 1;
    ? (1?) : ? : -;
    ? (0x) : ? : -;
    * ?    : ? : -;
  endtable
endprimitive
primitive latch (output reg q, input d, en);
  table
    1 1 : ? : 1;
    0 1 : ? : 0;
    ? 0 : ? : -;
  endtable
endprimitive
primitive edge_cnt (output reg q, input clk, rst);
  table
    ? 1 : ? : 0;
    (01) 0 : 0 : 1;
    (01) 0 : 1 : 0;
    (1?) 0 : ? : -;
    (?0) 0 : ? : -;
  endtable
endprimitive
module svlrm_c29a;
  logic a, b, s, d, clk, rst;
  wire y1, y2, q1, q2, q3;
  and2 u1 (y1, a, b);
  mux u2 (y2, s, a, b);
  dff u3 (q1, d, clk);
  latch u4 (q2, d, clk);
  edge_cnt u5 (q3, clk, rst);
  int errs;
  function automatic void ck(bit ok, string m);
    if (!ok) begin errs++; $error("%s", m); end
  endfunction
  initial begin
    #1 a = 0; b = 0; s = 0; d = 0; clk = 0; rst = 1;
    #1 ck(y1 === 1'b0 && y2 === 1'b0, "and2/mux with all-zero inputs");
    a = 1; b = 1; #1 ck(y1 === 1'b1, "and 1 1"); ck(y2 === 1'b1, "mux s=0 selects a");
    s = 1; b = 0; #1 ck(y2 === 1'b0, "mux s=1 selects b");
    a = 1'bx; b = 1; #1 ck(y1 === 1'bx, "and x 1 = x");
    a = 0; #1 ck(y1 === 1'b0, "and 0 1 = 0");
    d = 1; #1 clk = 1; #1 ck(q1 === 1'b1 && q2 === 1'b1, "rising edge loads dff, latch transparent");
    d = 0; #1 ck(q1 === 1'b1 && q2 === 1'b0, "dff holds on d change, latch follows d");
    clk = 0; #1 d = 1; #1 ck(q1 === 1'b1 && q2 === 1'b0, "clk low: both hold");
    clk = 1; #1 ck(q1 === 1'b1 && q2 === 1'b1, "second rising edge");
    rst = 0; #1 clk = 0; #1 clk = 1; #1 ck(q3 === 1'bx, "counter: no entry for rst 1->0, output x");
    clk = 0; #1 clk = 1; #1 ck(q3 === 1'bx, "counter stays x: no entry for state x on (01)");
    rst = 1; #1 ck(q3 === 1'b0, "rst=1 clears the counter");
    if (errs == 0) $display("PASSED");
  end
endmodule
