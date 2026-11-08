// Expected values: IEEE 1800-2017 6.18 (typedef), 7.4.1 (packed arrays of typedef'd packed types),
// 6.19 (enum values), 7.2.1 (packed structs), 13.4 (function return types), 23.3.3 (port connections), 20.6.2
package p;
  typedef logic [3:0] nib_t;
  typedef nib_t [1:0] byte_t;
  typedef enum logic [1:0] { A, B, C, D } e_t;
  typedef struct packed { nib_t [1:0] f; logic g; } s_t;
  function automatic nib_t [1:0] ff(nib_t x); return {x, x}; endfunction
  typedef enum logic [3:0] { M0 = 4'h5, M1 = 4'ha } m_t;
  typedef m_t [2:0] mv_t;
  typedef s_t [1:0] sv_t;
endpackage

module sub(input p::nib_t [1:0] a, output p::nib_t [1:0] b);
  assign b = a;
endmodule

module msub(input p::m_t [1:0] mi, output p::m_t [1:0] mo);
  assign mo = {mi[0], mi[1]};
endmodule

module svtypedims1;
  import p::*;
  byte_t v;
  s_t s;
  nib_t [1:0] w;
  p::nib_t [2:0] z;
  typedef nib_t [3:0] word_t;
  word_t u;
  sub u1(.a(8'h5a), .b(w));
  mv_t mv;
  p::m_t [1:0] mw;
  p::sv_t sa;
  p::m_t [1:0] mx = {M1, M0};
  msub u2(.mi(mx), .mo(mw));
  function automatic nib_t [1:0] loc(nib_t x); return {x, ~x}; endfunction
  int errs;
  initial begin
    v = 8'hA5; s.f = ff(4'h3); z = 12'habc; u = 16'h1234; #1;
    if ($bits(v) !== 8 || $bits(s) !== 9 || $bits(w) !== 8 || w !== 8'h5a) begin $error("l1 %0d %0d %0d %h", $bits(v), $bits(s), $bits(w), w); errs++; end
    if (v !== 8'ha5 || s.f !== 8'h33 || z !== 12'habc || u[2] !== 4'h2 || loc(4'h5) !== 8'h5a) begin $error("l2 %h %h %h %h %h", v, s.f, z, u[2], loc(4'h5)); errs++; end
    if ($bits(z) !== 12 || $bits(u) !== 16) begin $error("l3 %0d %0d", $bits(z), $bits(u)); errs++; end
    mv = {M0, M1, M0};
    sa[1].g = 1'b1; sa[0].g = 1'b0; sa[1].f = 8'h12;
    #1;
    if (mv !== 12'h5a5 || mw !== 8'h5a || $bits(mv) !== 12 || mv[1] !== M1 || sa[1].g !== 1'b1) begin $error("l4 %h %h %0d %h %b", mv, mw, $bits(mv), mv[1], sa[1].g); errs++; end
    if (sa[1].f !== 8'h12 || $bits(mw) !== 8) begin $error("l5 %h %0d", sa[1].f, $bits(mw)); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
