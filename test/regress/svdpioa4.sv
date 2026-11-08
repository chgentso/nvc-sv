// DPI open array formals with output and inout direction: what the C code
// writes is visible in the SV actual after the call.
// IEEE 1800-2017 35.5.6.1, 35.5.1.3 / 35.7 (output and inout arguments of
// imports), 7.7 (a dynamic array actual is allowed for an unsized inout
// formal; only an unsized output excludes it), Annex H.12: svGetArrElemPtr1 /
// svGetArrElemPtr2 for writes through a pointer, svPutBitArrElem1VecVal,
// svPutLogicArrElem1VecVal / svPutLogicArrElemVecVal,
// svPutBitArrElemVecVal, svPutBitArrElem1 / svPutBitArrElem,
// svPutLogicArrElem1.
// C side: test/dpi/dpi_openarr.c; the value written to each position is
// given in the comment of each C function and recomputed here.
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
module svdpioa4;
  import "DPI-C" function void oa_fill_int(output int x[], input int base);
  import "DPI-C" function int oa_scale_int(inout int x[]);
  import "DPI-C" function void oa_fill_b8(output bit [7:0] x[]);
  import "DPI-C" function void oa_inv_l4(inout logic [3:0] x[]);
  import "DPI-C" function void oa_fill_bit(output bit x[]);
  import "DPI-C" function void oa_fill_logic(output logic x[]);
  import "DPI-C" function void oa_fill_int2(output int x[][]);
  import "DPI-C" function void oa_rot_b70(inout bit [69:0] x[]);
  import "DPI-C" task oa_task_fill(output int x[], input int n);
  import "DPI-C" function int oa_errors();

  int err = 0;
  int o4[4];
  int od[3:0];
  int dyn[];
  bit [7:0] b8[2:4];
  logic [3:0] l4[3] = '{4'b01xz, 4'bzzzz, 4'b1100};
  logic [3:0] l4d[];
  bit bs[6];
  logic ls[5:0];
  int m[2][3];
  int md[1:0][3:2];
  bit [69:0] w[2] = '{70'h15_0000_0001_0000_0002, 70'h3f_ffff_ffff_0000_0000};
  int t[1:3];
  int n;

  initial begin
    oa_fill_int(o4, 100);
    `CHK(o4[0] == 100 && o4[1] == 101 && o4[2] == 104 && o4[3] == 109, "output int [4]")
    oa_fill_int(od, -5);
    `CHK(od[3] == -5 && od[2] == -4 && od[1] == -1 && od[0] == 4, "output int [3:0]")

    dyn = '{1, 2, 3};
    n = oa_scale_int(dyn);
    `CHK(n == 3 && dyn.size() == 3 && dyn[0] == 2 && dyn[1] == 5 && dyn[2] == 8,
         "inout int dynamic")
    n = oa_scale_int(o4);
    `CHK(o4[0] == 200 && o4[1] == 203 && o4[2] == 210 && o4[3] == 221, "inout int fixed")

    oa_fill_b8(b8);
    `CHK(b8[2] == 8'ha0 && b8[3] == 8'ha1 && b8[4] == 8'ha2, "output bit [7:0] [2:4]")

    // inverting the value word swaps 0/1 and x/z
    oa_inv_l4(l4);
    `CHK(l4[0] === 4'b10zx && l4[1] === 4'bxxxx && l4[2] === 4'b0011, "inout logic [3:0]")
    l4d = '{4'bx01z};
    oa_inv_l4(l4d);
    `CHK(l4d.size() == 1 && l4d[0] === 4'bz10x, "inout logic [3:0] dynamic")

    oa_fill_bit(bs);
    foreach (bs[i]) `CHK(bs[i] == (i & 1), "output bit scalars")
    oa_fill_logic(ls);
    `CHK(ls[5] === 1'b0 && ls[4] === 1'b1 && ls[3] === 1'bz && ls[2] === 1'bx
         && ls[1] === 1'b0 && ls[0] === 1'b1, "output logic scalars [5:0]")

    oa_fill_int2(m);
    foreach (m[i, j]) `CHK(m[i][j] == 10 * i + j, "output int [2][3]")
    oa_fill_int2(md);
    `CHK(md[1][3] == 0 && md[1][2] == 1 && md[0][3] == 10 && md[0][2] == 11,
         "output int [1:0][3:2]")

    // words w0, w1, w2 become w1, w2, w0[5:0]
    oa_rot_b70(w);
    `CHK(w[0] === {6'h02, 32'h0000_0015, 32'h0000_0001}, "inout bit [69:0] [0]")
    `CHK(w[1] === {6'h00, 32'h0000_003f, 32'hffff_ffff}, "inout bit [69:0] [1]")

    oa_task_fill(t, 50);
    `CHK(t[1] == 50 && t[2] == 49 && t[3] == 48, "imported task output int [1:3]")

    `CHK(oa_errors() == 0, "C side checks")
    if (err == 0) $display("PASSED");
  end
endmodule
