module vm(a0, a1, ax, az, b1, b0, c1, c0, e1, e0, s1, u1);
  output a0, a1, ax, az, b1, b0, c1, c0, e1, e0, s1, u1;
  reg d0 = 0, d1 = 1, dx = 1'bx, dz = 1'bz;
  triand a0, a1, ax, az;
  assign (weak1, strong0) a0 = d0;
  assign (weak1, strong0) a1 = d1;
  assign (weak1, strong0) ax = dx;
  assign (weak1, strong0) az = dz;
  assign (strong1, weak0) b1 = d1;
  assign (strong1, weak0) b0 = d0;
  assign (weak1, weak0) c1 = d1;
  assign (weak1, weak0) c0 = d0;
  assign (pull1, pull0) e1 = d1;
  assign (pull1, pull0) e0 = d0;
  assign (supply1, strong0) s1 = d1;
  assign (highz1, strong0) u1 = d1;
endmodule
