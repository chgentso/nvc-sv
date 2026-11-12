// release of a variable which was never forced has no effect: the variable
// keeps the value of its last assignment, whichever kind of process made it
// (IEEE 1800-2017 10.6.2). A forced variable keeps the forced value after
// release until the next assignment.
module svreleasenoforce1;
  logic [7:0] pre = 8'h5a;
  logic [7:0] b, c, d;
  logic [3:0][7:0] arr;
  int errs = 0;

  initial b = 8'h22;
  always_comb c = pre;
  always_comb begin
    for (int k = 0; k < 4; k++) arr[k] = pre + 8'(k);
  end
  always @(pre) d = pre;

  initial begin
    #1 pre = 8'h60;
    #1;
    release b;
    release c;
    release d;
    release arr;
    #2;
    if (b !== 8'h22) begin errs++; $error("b = %h", b); end
    if (c !== 8'h60) begin errs++; $error("c = %h", c); end
    if (d !== 8'h60) begin errs++; $error("d = %h", d); end
    if (arr !== 32'h63626160) begin errs++; $error("arr = %h", arr); end

    force c = 8'hee;
    #1 release c;
    #1 if (c !== 8'hee) begin errs++; $error("c after release of a force = %h", c); end
    pre = 8'h12;
    #1 if (c !== 8'h12) begin errs++; $error("c after next assignment = %h", c); end
    if (errs == 0) $display("PASSED");
  end
endmodule
