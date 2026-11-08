// DPI formals with an unsized packed dimension (bit [] / logic []), alone
// and together with an unsized unpacked dimension.
// IEEE 1800-2017 35.5.6.1 (the packed dimension of an import formal can be
// unsized; it takes the range of the actual), Annex H.12: dimension 0 is the
// packed dimension, svLeft/svRight/svSize(h, 0), svDimensions; the whole
// vector is reached with svGetArrayPtr in canonical svBitVecVal /
// svLogicVecVal form (H.7.7) when there is no unpacked dimension, and with
// svGetBitArrElem1VecVal otherwise.
// C side: test/dpi/dpi_openarr.c.
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
module svdpioa5;
  import "DPI-C" function int oa_q_pk(input bit [] v, input int d, input int what);
  import "DPI-C" function int oa_q_lpk(input logic [] v, input int d, input int what);
  import "DPI-C" function int oa_q_pku(input bit [] v[], input int d, input int what);
  import "DPI-C" function int oa_pk_pop(input bit [] v);
  import "DPI-C" function int oa_lpk_count(input logic [] v);
  import "DPI-C" function void oa_pk_fill(output bit [] v);
  import "DPI-C" function void oa_lpk_inv(inout logic [] v);
  import "DPI-C" function longint oa_pku_wsum(input bit [] v[]);
  import "DPI-C" function int oa_errors();

  // Reference for oa_pk_fill: bit k is set when k % 3 == 0, counting from
  // the least significant bit of the canonical form
  function automatic bit [127:0] every3(int width);
    every3 = 0;
    for (int k = 0; k < width; k++) every3[k] = k % 3 == 0;
  endfunction

  int err = 0;
  bit [7:0] b8 = 8'b1011_0001;
  bit [39:0] b40 = 40'hff_0000_0f0f;
  bit [69:0] b70 = {6'b10_0001, 64'hffff_ffff_ffff_ffff};
  bit [0:11] b12 = 12'hfff;
  bit b1 = 1;
  logic [9:0] l10 = 10'b1x0z_11xx_01;
  logic [33:0] l34 = {2'bx1, 32'h0000_0003};
  bit [8:0] o9;
  bit [64:0] o65;
  logic [5:0] io6 = 6'b01xz10;
  bit [11:0] u[3:1] = '{12'h001, 12'h010, 12'h100};
  bit [4:0] u5[2];

  initial begin
    `CHK(oa_q_pk(b8, 0, 6) == 1, "svDimensions of bit []")
    `CHK(oa_q_pk(b8, 0, 0) == 7 && oa_q_pk(b8, 0, 1) == 0, "left/right [7:0]")
    `CHK(oa_q_pk(b12, 0, 0) == 0 && oa_q_pk(b12, 0, 1) == 11, "left/right [0:11]")
    `CHK(oa_q_pk(b12, 0, 4) == -1, "increment [0:11]")
    `CHK(oa_q_pk(b70, 0, 5) == 70, "size 70")
    `CHK(oa_q_pk(b1, 0, 5) == 1, "size of a single bit")
    `CHK(oa_q_lpk(l10, 0, 5) == 10, "logic size")

    `CHK(oa_pk_pop(b8) == 4, "popcount 8")
    `CHK(oa_pk_pop(b40) == 16, "popcount 40")
    `CHK(oa_pk_pop(b70) == 66, "popcount 70")
    `CHK(oa_pk_pop(b12) == 12, "popcount [0:11]")
    `CHK(oa_pk_pop(b1) == 1, "popcount 1")
    `CHK(oa_pk_pop(8'h0f) == 4, "constant actual")

    // 1000 * unknown bits + one bits
    `CHK(oa_lpk_count(l10) == 4 * 1000 + 4, "logic [9:0]")
    `CHK(oa_lpk_count(l34) == 1 * 1000 + 3, "logic [33:0]")

    oa_pk_fill(o9);
    `CHK(o9 == every3(9), "output bit [8:0]")
    oa_pk_fill(o65);
    `CHK(o65 == every3(65), "output bit [64:0]")

    oa_lpk_inv(io6);
    `CHK(io6 === 6'b10zx01, "inout logic [5:0]")

    `CHK(oa_q_pku(u, 0, 6) == 2, "svDimensions of bit [] x[]")
    `CHK(oa_q_pku(u, 0, 5) == 12 && oa_q_pku(u, 1, 5) == 3, "sizes")
    `CHK(oa_q_pku(u, 1, 0) == 3 && oa_q_pku(u, 1, 1) == 1, "unpacked range")
    `CHK(oa_pku_wsum(u) == 12'h001 + 2 * 12'h010 + 3 * 12'h100, "bit [] x[] 12 bits")
    u5 = '{5'h1f, 5'h03};
    `CHK(oa_q_pku(u5, 0, 0) == 4, "packed left of bit [4:0] u5[2]")
    `CHK(oa_pku_wsum(u5) == 31 + 2 * 3, "bit [] x[] 5 bits")

    `CHK(oa_errors() == 0, "C side checks")
    if (err == 0) $display("PASSED");
  end
endmodule
