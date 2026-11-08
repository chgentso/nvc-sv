module svpatpos1;
  typedef struct packed { logic [3:0] a; logic [3:0] b; } s_t;
  s_t s;
  initial begin
    s = '{4'h3, 4'hc};
    if (s !== 8'h3c) $fatal(1, "s = %h", s);
    s = '{b: 4'h1, a: 4'h2};
    if (s !== 8'h21) $fatal(1, "s = %h", s);
    $display("PASSED");
  end
endmodule
