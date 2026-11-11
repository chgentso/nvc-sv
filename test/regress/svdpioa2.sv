// DPI open array formals whose elements are packed bit/logic vectors or
// scalars (input direction).
// IEEE 1800-2017 35.5.6.1, Annex H.12: dimension 0 is the packed dimension
// of a vector element and svDimensions counts only the unpacked
// dimensions (as existing DPI code expects); elements are read in the
// canonical svBitVecVal / svLogicVecVal form (H.7.7, 0=(a0,b0) 1=(a1,b0)
// z=(a0,b1) x=(a1,b1)) with svGetBitArrElem1VecVal / svGetBitArrElemVecVal,
// svGetLogicArrElem1VecVal / svGetLogicArrElemVecVal; scalar elements with
// svGetBitArrElem1 / svGetBitArrElem and svGetLogicArrElem1 /
// svGetLogicArrElem.
// C side: test/dpi/dpi_openarr.c; each returned checksum is described there
// and recomputed here from the canonical encoding.
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
module svdpioa2;
  import "DPI-C" function int oa_q_b8(input bit [7:0] x[], input int d, input int what);
  import "DPI-C" function int oa_q_l12(input logic [11:0] x[], input int d, input int what);
  import "DPI-C" function int oa_q_bit(input bit x[], input int d, input int what);
  import "DPI-C" function longint oa_wsum_b8(input bit [7:0] x[]);
  import "DPI-C" function longint oa_wsum_l12(input logic [11:0] x[]);
  import "DPI-C" function longint oa_wsum_l40(input logic [39:0] x[]);
  import "DPI-C" function longint oa_wsum_b70(input bit [69:0] x[]);
  import "DPI-C" function int oa_bits(input bit x[]);
  import "DPI-C" function int oa_logics(input logic x[]);
  import "DPI-C" function int oa_errors();

  // Canonical value (aval) and unknown (bval) words of a logic vector
  function automatic logic [63:0] aval(logic [63:0] v);
    for (int i = 0; i < 64; i++) aval[i] = v[i] === 1'b1 || v[i] === 1'bx;
  endfunction
  function automatic logic [63:0] bval(logic [63:0] v);
    for (int i = 0; i < 64; i++) bval[i] = v[i] === 1'bz || v[i] === 1'bx;
  endfunction
  function automatic int enc(logic b);   // svLogic encoding
    return b === 1'b0 ? 0 : b === 1'b1 ? 1 : b === 1'bz ? 2 : 3;
  endfunction

  int err = 0;
  bit [7:0] b8[4] = '{8'h01, 8'h80, 8'hff, 8'h3c};
  bit [7:0] b8d[2:1] = '{8'h11, 8'h22};
  logic [11:0] l12[2:0] = '{12'hx0f, 12'b1z0x_0000_1111, 12'hzzz};
  logic [39:0] l40[2] = '{40'hx1_2345_678z, 40'h80_0000_0001};
  bit [69:0] b70[0:1] = '{70'h3f_1234_5678_9abc_def0, 70'h1};
  bit s5[5] = '{1, 0, 1, 1, 0};
  bit sd[];
  logic sl[1:4] = '{1'b0, 1'bx, 1'b1, 1'bz};
  longint e;

  initial begin
    `CHK(oa_q_b8(b8, 0, 6) == 1, "svDimensions of bit [7:0] x[]")
    `CHK(oa_q_b8(b8, 0, 0) == 7 && oa_q_b8(b8, 0, 1) == 0, "packed left/right")
    `CHK(oa_q_b8(b8, 0, 5) == 8, "packed size")
    `CHK(oa_q_b8(b8d, 1, 0) == 2 && oa_q_b8(b8d, 1, 1) == 1, "unpacked left/right")
    `CHK(oa_q_l12(l12, 0, 5) == 12 && oa_q_l12(l12, 1, 5) == 3, "logic sizes")
    `CHK(oa_q_bit(s5, 0, 6) == 1, "svDimensions of bit x[]")

    `CHK(oa_wsum_b8(b8) == 8'h01 + 2 * 8'h80 + 3 * 8'hff + 4 * 8'h3c, "bit [7:0] [4]")
    `CHK(oa_wsum_b8(b8d) == 8'h11 + 2 * 8'h22, "bit [7:0] [2:1]")

    e = 0;
    for (int i = 2, p = 0; i >= 0; i--, p++)
      e += (p + 1) * (aval(l12[i]) + (bval(l12[i]) << 16));
    `CHK(oa_wsum_l12(l12) == e, "logic [11:0] [2:0]")

    e = 0;
    for (int p = 0; p < 2; p++) begin
      automatic logic [63:0] a = aval(l40[p]), b = bval(l40[p]);
      e += (p + 1) * (a[31:0] + 3 * a[39:32] + 5 * b[31:0] + 7 * b[39:32]);
    end
    `CHK(oa_wsum_l40(l40) == e, "logic [39:0] (two words per element)")

    e = 0;
    for (int p = 0; p < 2; p++)
      e += (p + 1) * (longint'(b70[p][31:0]) + 3 * longint'(b70[p][63:32])
                      + 5 * longint'(b70[p][69:64]));
    `CHK(oa_wsum_b70(b70) == e, "bit [69:0] (three words per element)")

    `CHK(oa_bits(s5) == 5'b01101, "scalar bit elements (position p -> bit p)")
    sd = '{0, 0, 0, 1, 1, 1, 1, 1, 1};
    `CHK(oa_bits(sd) == 9'b111111000, "scalar bit dynamic array")
    `CHK(oa_logics(sl) == (enc(1'b0) | enc(1'bx) << 2 | enc(1'b1) << 4 | enc(1'bz) << 6),
         "scalar logic elements")
    `CHK(oa_logics(sl) == 8'b10_01_11_00, "scalar logic constant")

    `CHK(oa_errors() == 0, "C side checks")
    if (err == 0) $display("PASSED");
  end
endmodule
