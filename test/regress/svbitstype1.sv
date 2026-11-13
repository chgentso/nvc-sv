// Self-checking. IEEE 1800-2017 20.6.2: $bits of a data type whose size
// depends on a parameter can be used in a continuous assignment.
module svbitstype1_sub #(parameter int W = 5) (output int o);
  typedef logic [W-1:0] t_t;
  typedef struct packed { t_t a; logic [3:0] b; } s_t;
  assign o = $bits(t_t) + $bits(s_t);
endmodule

module svbitstype1;
  int o1, o2;
  svbitstype1_sub #(7) u1(o1);
  svbitstype1_sub u2(o2);
  initial begin
    #1;
    if (o1 !== 18 || o2 !== 14)
      $error("FAIL %0d %0d", o1, o2);
    else
      $display("PASSED");
  end
endmodule
