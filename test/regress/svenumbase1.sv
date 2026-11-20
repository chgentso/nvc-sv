module svenumbase1;
  typedef enum [2:0] { E0 = 0, E1 = 1, EMAX = 7 } u3_e;
  typedef enum signed [3:0] { SNEG = -2, SZERO = 0, SPOS = 3 } s4_e;

  u3_e u = EMAX;
  s4_e s = SNEG;

  initial begin
    if ($bits(u3_e) !== 3) $fatal(1, "expected width 3 got %0d", $bits(u3_e));
    if (u !== 3'd7) $fatal(1, "expected 7 got %0d", u);
    if ($bits(s4_e) !== 4) $fatal(1, "expected width 4 got %0d", $bits(s4_e));
    if (s !== -2) $fatal(1, "expected -2 got %0d", s);
    $display("PASSED");
    $finish;
  end
endmodule
