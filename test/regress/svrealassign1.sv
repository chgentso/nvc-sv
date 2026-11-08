// A continuous assignment to a real variable delivers the exact value
// IEEE 1800-2017 10.3.2 (continuous assignment to a variable), 6.12
// (real data types), 23.2.2 (real ports)
module sub (input real x, input real y, output real sum);
  assign sum = x + y;
endmodule

module svrealassign1;
  real a, b, s, l, m, n;
  int  err = 0;
  sub u (.x(a), .y(b), .sum(s));
  assign l = a;
  assign m = a * b;
  assign n = 6.5;
  initial begin
    a = 1.0; b = 5.5;
    #1;
    if (s != 6.5 || $realtobits(s) != 64'h401a000000000000) err++;
    if (l != 1.0 || m != 5.5 || n != 6.5) err++;
    a = -0.1;
    #1;
    if (s != -0.1 + 5.5 || l != -0.1 || m != -0.1 * 5.5) err++;
    if (err == 0) $display("PASSED");
    else $display("FAILED %0d", err);
  end
endmodule
