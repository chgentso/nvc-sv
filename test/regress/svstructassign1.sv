// Expected values: IEEE 1800-2017 10.3 (continuous assignments re-evaluate
// when an operand changes), 10.9.2 (structure assignment patterns), 6.12.
module svstructassign1;
  typedef struct { real v; int n; } sum_t;
  sum_t s;
  real a = 1.5;
  int errs;
  assign s = '{a, 1};
  initial begin
    #1;
    if (s.v != 1.5 || s.n != 1) begin $error("t=1: s = {%f,%0d}, expected {1.5,1}", s.v, s.n); errs++; end
    a = 7.0; #1;
    if (s.v != 7.0 || s.n != 1) begin $error("t=2: s = {%f,%0d}, expected {7.0,1}", s.v, s.n); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
