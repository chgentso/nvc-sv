// Procedural continuous assignment (assign/deassign) and force/release on variables and nets.
// Expected values: IEEE 1800-2017 10.6.1 (force/release; force expression is continuously
// re-evaluated; net reverts to its driver on release), 10.6.2 (assign overrides procedural
// assignments until deassign; deassign leaves the variable's value; release of a variable
// with an active assign re-establishes the assign), 4-bit arithmetic of e+d.
`timescale 1ns/1ns
module svprocassign1;
  reg clk = 0, rst = 0, en = 0;
  reg [3:0] d = 0, e = 5, r, s, t;
  wire [3:0] w;
  reg [3:0] ww;
  int errs = 0;
  assign w = ww;
  always #5 clk = ~clk;
  always @(posedge clk) begin
    if (rst) deassign r;
    else assign r = d;
  end
  always @(en or e) if (en) assign s = e + d; else deassign s;
  task automatic chk(string what, logic [3:0] got, logic [3:0] exp);
    if (got !== exp) begin errs++; $error("%s: got %h expected %h", what, got, exp); end
  endtask
  initial begin
    r = 4'hf; s = 4'h0; ww = 4'h2;
    #12 d = 4'h3;
    #1 chk("r tracks d", r, 4'h3); chk("s idle", s, 4'h0);
    #10 d = 4'h9; en = 1;
    #1 chk("r d=9", r, 4'h9); chk("s=e+d", s, 4'he);
    e = 4'h1;
    #1 chk("r", r, 4'h9); chk("s=1+9", s, 4'ha);
    r = 4'h4;
    #1 chk("write while assigned", r, 4'h9);
    force r = e;
    #1 chk("forced", r, 4'h1);
    e = 4'h7;
    #1 chk("forced follows e", r, 4'h7);
    release r;
    #1 chk("released: assign re-established", r, 4'h9);
    e = 4'h2;
    #1 chk("after e", r, 4'h9);
    r = 4'h6;
    #1 chk("assign overrides write", r, 4'h9);
    force w = e;
    #1 chk("w forced", w, 4'h2);
    e = 4'h8;
    #1 chk("w follows e", w, 4'h8);
    release w;
    #1 chk("w released to driver", w, 4'h2);
    chk("s tracked e=8", s, 4'h1);
    rst = 1; en = 0;
    #10 d = 4'h5;
    #1 chk("r deassigned keeps value", r, 4'h9); chk("s deassigned keeps value", s, 4'h1);
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
