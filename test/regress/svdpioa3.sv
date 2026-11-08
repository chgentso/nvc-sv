// DPI open array formals with several unpacked dimensions.
// IEEE 1800-2017 35.5.6.1 (each unsized unpacked dimension takes the range
// of the corresponding dimension of the actual), Annex H.12: unpacked
// dimensions numbered 1.. from the left, svDimensions counts the packed
// dimension of a vector element, svGetArrElemPtr2 / svGetArrElemPtr,
// svGetBitArrElem3VecVal / svGetBitArrElemVecVal, svGetLogicArrElem2 /
// svGetLogicArrElem, svGetArrayPtr in row-major order from the leftmost
// element (H.7.5).
// C side: test/dpi/dpi_openarr.c; the checksum is sum of (p + 1) * element
// where p is the row-major position counted from the left bounds.
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
module svdpioa3;
  import "DPI-C" function int oa_q_int2(input int x[][], input int d, input int what);
  import "DPI-C" function int oa_q_b8x3(input bit [7:0] x[][][], input int d, input int what);
  import "DPI-C" function longint oa_wsum_int2(input int x[][]);
  import "DPI-C" function longint oa_wsum_b8x3(input bit [7:0] x[][][]);
  import "DPI-C" function int oa_logics2(input logic x[][]);
  import "DPI-C" function int oa_errors();

  int err = 0;
  int m[2][3] = '{'{1, 2, 3}, '{4, 5, 6}};
  int md[1:0][2:0] = '{'{1, 2, 3}, '{4, 5, 6}};     // md[1][2] = 1
  int mm[3:1][0:1] = '{'{-1, 1}, '{100, 200}, '{7, 0}};
  bit [7:0] c3[2][2][3];
  bit [7:0] c3d[1:0][0:1][5:3];
  logic l2[2][2] = '{'{1'b1, 1'bz}, '{1'bx, 1'b0}};
  longint e;
  int p;

  initial begin
    `CHK(oa_q_int2(md, 1, 0) == 1 && oa_q_int2(md, 1, 1) == 0, "dim 1 [1:0]")
    `CHK(oa_q_int2(md, 2, 0) == 2 && oa_q_int2(md, 2, 1) == 0, "dim 2 [2:0]")
    `CHK(oa_q_int2(mm, 1, 5) == 3 && oa_q_int2(mm, 2, 5) == 2, "sizes [3:1][0:1]")
    `CHK(oa_q_int2(mm, 2, 4) == -1 && oa_q_int2(mm, 1, 4) == 1, "increments")

    // Row-major order from the left bounds is the same as the order of the
    // assignment pattern
    `CHK(oa_wsum_int2(m) == 1*1 + 2*2 + 3*3 + 4*4 + 5*5 + 6*6, "int [2][3]")
    `CHK(oa_wsum_int2(md) == 1*1 + 2*2 + 3*3 + 4*4 + 5*5 + 6*6, "int [1:0][2:0]")
    `CHK(oa_wsum_int2(mm) == -1 + 2 + 300 + 800 + 35 + 0, "int [3:1][0:1]")

    e = 0;
    p = 0;
    foreach (c3[i, j, k]) begin
      c3[i][j][k] = 8'(i * 64 + j * 16 + k * 3 + 1);
      e += (p + 1) * c3[i][j][k];
      p++;
    end
    `CHK(oa_q_b8x3(c3, 0, 6) == 4, "svDimensions of bit [7:0] x[][][]")
    `CHK(oa_q_b8x3(c3, 3, 5) == 3, "dim 3 size")
    `CHK(oa_wsum_b8x3(c3) == e, "bit [7:0] [2][2][3]")

    // foreach walks each dimension from its left bound to its right bound
    e = 0;
    p = 0;
    foreach (c3d[i, j, k]) begin
      c3d[i][j][k] = 8'(i * 50 + j * 20 + k);
      e += (p + 1) * c3d[i][j][k];
      p++;
    end
    `CHK(oa_q_b8x3(c3d, 3, 0) == 5 && oa_q_b8x3(c3d, 3, 1) == 3, "dim 3 [5:3]")
    `CHK(oa_wsum_b8x3(c3d) == e, "bit [7:0] [1:0][0:1][5:3]")

    // svLogic of 1, z, x, 0 at row-major positions 0..3
    `CHK(oa_logics2(l2) == (1 | 2 << 2 | 3 << 4 | 0 << 6), "logic [2][2]")

    `CHK(oa_errors() == 0, "C side checks")
    if (err == 0) $display("PASSED");
  end
endmodule
