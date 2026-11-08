// DPI formals that are sized (not open) unpacked arrays.
// IEEE 1800-2017 35.5.6 (unpacked arrays are passed as C arrays), Annex
// H.7.5 (the C array is indexed from 0 for the leftmost element of the SV
// range, multi-dimensional arrays in row-major order), H.7.7 (each element
// of a packed vector array occupies SV_PACKED_DATA_NELEMS(width) words of
// svBitVecVal / svLogicVecVal), 7.7 (a dynamic array of the same size may be
// passed to a sized formal, checked at run time).
// C side: test/dpi/dpi_openarr.c (fx_*).
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
module svdpioa6;
  import "DPI-C" function longint fx_wsum4(input int x[4]);
  import "DPI-C" function longint fx_wsum23(input int m[2][3]);
  import "DPI-C" function longint fx_bytes(input byte b[1:4]);
  import "DPI-C" function void fx_out_b8(output bit [7:0] y[3]);
  import "DPI-C" function void fx_inv_l4(inout logic [3:0] z[2]);
  import "DPI-C" function int fx_pop40(input bit [39:0] w[2], output int sums[2]);

  int err = 0;
  int a[4] = '{1, 2, 3, 4};
  int ad[3:0] = '{1, 2, 3, 4};      // ad[3] = 1 is the leftmost element
  int dyn[];
  int m[1:0][0:2] = '{'{10, 20, 30}, '{-1, -2, -3}};
  byte b[4:1] = '{-1, -2, 3, 4};
  bit [7:0] y[0:2];
  bit [7:0] yd[2:0];
  logic [3:0] z[1:0] = '{4'b01xz, 4'b1z0x};
  bit [39:0] w[2] = '{40'h01_ffff_ffff, 40'h80_0000_0003};
  int sums[2];

  initial begin
    `CHK(fx_wsum4(a) == 1 + 4 + 9 + 16, "int [4]")
    `CHK(fx_wsum4(ad) == 1 + 4 + 9 + 16, "int [3:0] actual for int [4] formal")
    dyn = '{4, 3, 2, 1};
    `CHK(fx_wsum4(dyn) == 4 + 6 + 6 + 4, "dynamic array of the same size")
    `CHK(fx_wsum23(m) == 10 + 40 + 90 - 4 - 10 - 18, "int [2][3]")
    `CHK(fx_bytes(b) == -1 - 4 + 9 + 16, "byte [4:1] actual for byte [1:4] formal")

    fx_out_b8(y);
    `CHK(y[0] == 8'h30 && y[1] == 8'h31 && y[2] == 8'h32, "output bit [7:0] [3]")
    fx_out_b8(yd);
    `CHK(yd[2] == 8'h30 && yd[0] == 8'h32, "output into [2:0]")

    fx_inv_l4(z);
    `CHK(z[1] === 4'b10zx && z[0] === 4'b0x1z, "inout logic [3:0] [2]")

    `CHK(fx_pop40(w, sums) == 33 + 2 + 1, "popcount of bit [39:0] [2]")
    `CHK(sums[0] == int'(32'hffff_ffff + 32'h01), "word sum 0")
    `CHK(sums[1] == 32'h0000_0003 + 32'h80, "word sum 1")

    if (err == 0) $display("PASSED");
  end
endmodule
