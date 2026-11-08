// IEEE 1800-2017 23.6 (hierarchical names) and 6.12 (real): a real
// variable read through a hierarchical name has its value, including the
// value of its initialiser (6.8).
module svhierreal1_sub;
  real t = -1.0;
  real u;
  shortreal s = 0.5;
  initial u = 2.5;
endmodule

module svhierreal1;
  svhierreal1_sub i();
  real x;
  int errs;
  initial begin
    #1;
    x = i.t;
    if (x != -1.0) begin $error("i.t = %f, expected -1.0", x); errs++; end
    if (i.u != 2.5) begin $error("i.u = %f, expected 2.5", i.u); errs++; end
    if (i.s != 0.5) begin $error("i.s = %f, expected 0.5", i.s); errs++; end
    i.t = 3.5;
    if (i.t + 1.0 != 4.5) begin $error("i.t + 1.0 = %f, expected 4.5", i.t + 1.0); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
