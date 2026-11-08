// Expected values: IEEE 1800-2017 6.23 (type operator: type(a+b) is real, type(x+y) is the 8-bit result type of
// the expression, type(x[3:0]) is a 4-bit logic vector), 11.6 (expression bit lengths), 6.12 (real)
module svtypeop1;
  real a = 4.76;
  real b = 0.74;
  var type(a+b) c;
  logic [7:0] x = 8'hf0;
  logic [3:0] y = 4'h3;
  var type(x + y) s;
  var type(x[3:0]) q;
  int errs;
  initial begin
    c = a + b;
    s = x + y;
    q = x[7:4];
    if (((c > 5.5) ? (c - 5.5) : (5.5 - c)) > 1.0e-9) begin $error("c %f", c); errs++; end
    if ($typename(c) != "real") begin $error("type of c %s", $typename(c)); errs++; end
    if (s !== 8'd243 || $bits(s) !== 8) begin $error("s %0d bits %0d", s, $bits(s)); errs++; end
    if (q !== 4'd15 || $bits(q) !== 4) begin $error("q %0d bits %0d", q, $bits(q)); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
