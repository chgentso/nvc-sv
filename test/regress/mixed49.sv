module mixed49;
  reg d0 = 0, d1 = 1, dx = 1'bx, dz = 1'bz;
  triand (weak1, strong0) a0 = d0;
  triand (weak1, strong0) a1 = d1;
  triand (weak1, strong0) ax = dx;
  triand (weak1, strong0) az = dz;
  wire (weak1, weak0) c1 = d1;
  wire (weak1, weak0) c0 = d0;
  wire (pull1, pull0) e1 = d1;
  wire (pull1, pull0) e0 = d0;
  wire (highz1, strong0) u1 = d1;
  wire (highz1, strong0) u0 = d0;
  wire (strong1, highz0) v1 = d1;
  wire (strong1, highz0) v0 = d0;
  wire (supply1, supply0) s1 = d1;
  wire (supply1, supply0) s0 = d0;
  wire (strong1, weak0) w1 = d1;
  wire (strong1, weak0) w0 = d0;
  wire [3:0] vec;
  assign (weak1, weak0) vec[0] = 1;
  assign (pull1, pull0) vec[1] = 0;
  assign vec[2] = 1;
  assign (strong1, highz0) vec[3] = 0;
  wire plain = d1;
  wire plainx = dx;
  mixed49_e r(.p_a0(a0), .p_a1(a1), .p_ax(ax), .p_az(az), .p_c1(c1), .p_c0(c0),
              .p_e1(e1), .p_e0(e0), .p_u1(u1), .p_u0(u0), .p_v1(v1), .p_v0(v0),
              .p_s1(s1), .p_s0(s0), .p_w1(w1), .p_w0(w0), .p_vec(vec),
              .p_hi(vec[3:2]), .p_bit(vec[1]), .p_plain(plain),
              .p_plainx(plainx));
endmodule
