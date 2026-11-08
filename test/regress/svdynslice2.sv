// Assignment to a slice of a dynamic array.
// Expected values: IEEE 1800-2017 7.4.3 (array slices as assignment targets: the
// elements of the slice are replaced, others are unchanged), 7.5, 7.10.
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
typedef int unsigned wrtd_data[];
module svdynslice2;
  wrtd_data ret, s4, q;
  int e[$];
  int err = 0;
  initial begin
    ret = new[8];
    foreach (ret[i]) ret[i] = 100 + i;
    s4 = new[4];
    foreach (s4[i]) s4[i] = i + 1;
    ret[2:5] = s4;
    `CHK(ret[0] == 100 && ret[1] == 101 && ret[2] == 1 && ret[3] == 2, "ret[0:3]")
    `CHK(ret[4] == 3 && ret[5] == 4 && ret[6] == 106 && ret[7] == 107, "ret[4:7]")
    ret[6:7] = s4[1:2];
    `CHK(ret[6] == 2 && ret[7] == 3, "ret[6:7]")
    e = '{9, 8, 7, 6};
    e[1:2] = '{1, 2};
    `CHK(e[0] == 9 && e[1] == 1 && e[2] == 2 && e[3] == 6, "queue slice")
    `CHK(ret.size() == 8 && e.size() == 4, "sizes unchanged")
    if (err == 0) $display("PASSED");
  end
endmodule
