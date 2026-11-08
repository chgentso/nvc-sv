// Self-checking. Expected values: IEEE 1800-2017 6.11.3 (byte, shortint, longint and int are signed),
// 6.11.1 / 11.8.2 (a signed operand is sign-extended when widened; assignment of a signed value to a wider
// integral variable sign-extends), 11.8.1 (comparison of two signed operands is signed), 12.7.3 (foreach),
// 7.4 (array element types keep their signedness), 18.4 (rand does not change the element type).
`define CHK(tag, got, exp) if ((got) !== (exp)) begin errs++; $error("%s: got %0d expected %0d", tag, got, exp); end
class k;
  byte c [3];
  rand byte r [3];
  byte sc;
endclass
module svarrsigned1;
  int errs = 0;
  byte arr [3] = '{-1, 2, -3};
  byte q [$] = '{-1, 2};
  byte dyn [] = '{-4, 5};
  bit signed [7:0] pv [3] = '{-1, 2, -3};
  int s, s2, s3, s4, s5, s6, t, v, x, y, z;
  k o = new;
  shortint sh [2] = '{-5, 7};
  longint lg [2] = '{-9, 1};
  int ml [2][2] = '{'{-1, 2}, '{-3, 4}};
  byte mb [2][2] = '{'{-1, 2}, '{-3, 4}};
  int m1, m2;
  initial begin
    o.c = '{-1, 2, -3};
    o.r = '{-1, 2, -3};
    foreach (arr[i]) s += arr[i];
    foreach (o.c[i]) s2 += o.c[i];
    foreach (o.r[i]) s3 += o.r[i];
    for (int i = 0; i < 3; i++) s4 = s4 + o.r[i];
    for (int i = 0; i < 2; i++) s5 += sh[i];
    for (int i = 0; i < 3; i++) s6 += o.r[i] * 2;
    `CHK("s", s, -2) `CHK("s2", s2, -2) `CHK("s3", s3, -2) `CHK("s4", s4, -2) `CHK("s5", s5, 2) `CHK("s6", s6, -4)
    t = q[0]; v = dyn[0]; x = pv[0]; y = int'(arr[0]); z = arr[0] + 1;
    `CHK("t", t, -1) `CHK("v", v, -4) `CHK("x", x, -1) `CHK("y", y, -1) `CHK("z", z, 0) `CHK("arr[0]", arr[0], -1)
    foreach (mb[i, j]) m1 += mb[i][j];
    foreach (ml[i, j]) m2 += ml[i][j];
    `CHK("m1", m1, 2) `CHK("m2", m2, 2) `CHK("lg[0]+1", lg[0] + 1, -8)
    `CHK("signed compare", arr[0] < 0 && arr[2] < arr[1] && pv[2] < 0, 1'b1)
    arr[1] = arr[0];
    `CHK("arr[1]", arr[1], -1)
    if (errs == 0) $display("PASSED");
  end
endmodule
