// DPI open array formals with integer and real elements (input direction).
// IEEE 1800-2017 35.5.6.1 (open arrays: unsized unpacked dimension of an
// import formal takes its range from the actual), 7.7 (an open array formal
// accepts a fixed-size or dynamic array of any size), Annex H.12 (query
// functions svLeft/svRight/svLow/svHigh/svIncrement/svSize, svGetArrayPtr,
// svSizeOfArray, svGetArrElemPtr/svGetArrElemPtr1), H.7.5 (C layout of an
// unpacked array starts at the leftmost element).
// C side: test/dpi/dpi_openarr.c.  The C functions return a weighted sum
// (sum over positions p from the left of (p + 1) * element); the expected
// value is computed here from a dynamic array copy, which keeps the
// left-to-right order of the actual (7.6).
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
module svdpioa1;
  import "DPI-C" function int oa_q_int(input int x[], input int d, input int what);
  import "DPI-C" function int oa_q_byte(input byte x[], input int d, input int what);
  import "DPI-C" function longint oa_wsum_int(input int x[]);
  import "DPI-C" function longint oa_wsum_byte(input byte x[]);
  import "DPI-C" function longint oa_wsum_long(input longint x[]);
  import "DPI-C" function real oa_wsum_real(input real x[]);
  import "DPI-C" function int oa_errors();

  function automatic longint ref_int(int d[]);
    longint s = 0;
    foreach (d[p]) s += (p + 1) * d[p];
    return s;
  endfunction

  function automatic longint ref_long(longint d[]);
    longint s = 0;
    foreach (d[p]) s += (p + 1) * d[p];
    return s;
  endfunction

  function automatic real ref_real(real d[]);
    real s = 0;
    foreach (d[p]) s += (p + 1) * d[p];
    return s;
  endfunction

  int err = 0;
  int a4[4] = '{10, -20, 30, 40};          // [0:3]
  int ad[3:0] = '{5, 6, 7, 8};             // descending: ad[3] = 5
  int aa[2:6] = '{1, 2, 3, 4, 5};          // ascending, not from 0
  int a1[1] = '{77};
  int dyn[];
  byte b[5] = '{-1, 2, -3, 4, 127};
  byte bd[];
  longint l[2:1] = '{64'h1_0000_0000, -3};
  real r[3] = '{1.5, -2.25, 4.0};
  int k;

  initial begin
    // Queries on dimension 1 follow the actual's declared range
    `CHK(oa_q_int(ad, 1, 0) == 3 && oa_q_int(ad, 1, 1) == 0, "left/right [3:0]")
    `CHK(oa_q_int(ad, 1, 2) == 0 && oa_q_int(ad, 1, 3) == 3, "low/high [3:0]")
    `CHK(oa_q_int(ad, 1, 4) == 1 && oa_q_int(ad, 1, 5) == 4, "increment/size [3:0]")
    `CHK(oa_q_int(aa, 1, 0) == 2 && oa_q_int(aa, 1, 1) == 6, "left/right [2:6]")
    `CHK(oa_q_int(aa, 1, 2) == 2 && oa_q_int(aa, 1, 3) == 6, "low/high [2:6]")
    `CHK(oa_q_int(aa, 1, 4) == -1 && oa_q_int(aa, 1, 5) == 5, "increment/size [2:6]")
    `CHK(oa_q_int(a4, 1, 0) == 0 && oa_q_int(a4, 1, 1) == 3, "left/right [4]")
    `CHK(oa_q_int(a4, 1, 4) == $increment(a4), "increment matches $increment")
    `CHK(oa_q_int(ad, 1, 4) == $increment(ad), "increment matches $increment (desc)")
    `CHK(oa_q_byte(b, 1, 5) == 5, "byte size")

    `CHK(oa_wsum_int(a4) == ref_int(a4), "int [4]")
    `CHK(oa_wsum_int(a4) == 10 - 40 + 90 + 160, "int [4] constant")
    `CHK(oa_wsum_int(ad) == ref_int(ad), "int [3:0]")
    `CHK(oa_wsum_int(ad) == 5 + 12 + 21 + 32, "int [3:0] constant")
    `CHK(oa_wsum_int(aa) == ref_int(aa), "int [2:6]")
    `CHK(oa_wsum_int(a1) == 77, "int [1]")

    // Dynamic array actuals (7.7): range [0:size-1]
    dyn = new[6];
    foreach (dyn[i]) dyn[i] = i * i - 3;
    `CHK(oa_q_int(dyn, 1, 0) == 0 && oa_q_int(dyn, 1, 1) == 5, "dynamic range")
    `CHK(oa_q_int(dyn, 1, 4) == -1, "dynamic increment")
    `CHK(oa_wsum_int(dyn) == ref_int(dyn), "int dynamic")
    dyn = new[1000];
    foreach (dyn[i]) dyn[i] = i;
    `CHK(oa_q_int(dyn, 1, 5) == 1000, "resized dynamic size")
    `CHK(oa_wsum_int(dyn) == ref_int(dyn), "int dynamic 1000")

    `CHK(oa_wsum_byte(b) == -1 + 4 - 9 + 16 + 5 * 127, "byte")
    bd = '{-128, 1};
    `CHK(oa_wsum_byte(bd) == -128 + 2, "byte dynamic")
    `CHK(oa_wsum_long(l) == ref_long(l), "longint [2:1]")
    `CHK(oa_wsum_long(l) == 64'h1_0000_0000 - 6, "longint constant")
    `CHK(oa_wsum_real(r) == ref_real(r), "real")
    `CHK(oa_wsum_real(r) == 1.5 - 4.5 + 12.0, "real constant")

    // The C side may not change an input open array
    k = oa_wsum_int(a4);
    `CHK(a4[0] == 10 && a4[1] == -20 && a4[2] == 30 && a4[3] == 40, "input unchanged")

    `CHK(oa_errors() == 0, "C side checks")
    if (err == 0) $display("PASSED");
  end
endmodule
