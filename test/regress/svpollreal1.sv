// A continuous assignment from a real member of an unpacked structure
// Expected values: IEEE 1800-2017 10.3.2 (a continuous assignment is
// re-evaluated when an operand changes), 7.2 (structure members)
typedef struct { real r; int n; } st_t;
module svpollreal1;
  st_t s;
  real x;
  assign x = s.r;
  initial begin
    s.r = 2.5;
    #1 if (x != 2.5) $error("x = %f", x);
    s.r = -0.75;
    #1 if (x != -0.75) $error("x = %f", x);
    else $display("PASSED");
  end
endmodule
