// Bit and part selects of elements of dynamic arrays and queues and
// unpacked array operands of a queue concatenation.
// Expected values: IEEE 1800-2017 7.4.6 / 11.5.1 (select of an element of an unpacked
// array), 7.10.4 (unpacked array concatenation into a queue), 10.10, 7.4.3 (array slices).
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
module svdynsel1;
  typedef logic [7:0] t_b [2:4];
  t_b bb;
  logic [7:0] dd [];
  logic [7:0] qq [$];
  int p [1:4] = '{10,20,30,40};
  int da[] = '{1,2,3,4};
  int q[$] = {1,2,3,4};
  int err = 0;
  initial begin
    bb[2] = 8'h12; bb[4] = 8'h34;
    dd = new[2]; dd[1] = 8'h81;
    qq = {8'h0f, 8'h70};
    `CHK(bb[2][1] === 1'b1, "bb[2][1]")
    `CHK(bb[4][7:4] === 4'h3, "bb[4][7:4]")
    `CHK(dd[1][7:4] === 4'h8, "dd[1][7:4]")
    dd[1][3:0] = 5;
    dd[1][7] = 0;
    `CHK(dd[1] === 8'h05, "dd[1] after part write")
    qq[1][6:4] = 3'b101;
    `CHK(qq[1] === 8'h50, "qq[1] after part write")
    `CHK(qq[0][3] === 1'b1, "qq[0][3]")
    bb[3][3:0] = 4'h5; bb[3][7:4] = 4'ha;
    `CHK(bb[3] === 8'ha5, "bb[3]")
    q = {p}; `CHK(q.size() == 4 && q[3] == 40, "q = {p}")
    q = {p[2:3], 5}; `CHK(q.size() == 3 && q[0] == 20 && q[1] == 30 && q[2] == 5, "q = {p[2:3], 5}")
    da[1:2] = {5, 6}; `CHK(da[0] == 1 && da[1] == 5 && da[2] == 6 && da[3] == 4, "da slice write")
    q[1:2] = {7, 8}; `CHK(q[0] == 20 && q[1] == 7 && q[2] == 8, "q slice write")
    p[2:3] = {7, 8}; `CHK(p[1] == 10 && p[2] == 7 && p[3] == 8 && p[4] == 40, "p slice write")
    if (err == 0) $display("PASSED");
  end
endmodule
