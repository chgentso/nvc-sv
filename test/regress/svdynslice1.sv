// Slice of a dynamic array as an argument and as the source of an assignment.
// Expected values: IEEE 1800-2017 7.4.3 (array slices: a slice [a:b] has b-a+1 elements),
// 7.5 (dynamic arrays), 13.5 (argument passing).  A slice that lies partly outside the
// array is not specified in the standard and is not checked.
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
typedef int unsigned wrtd_data[];
module svdynslice1;
  function automatic int sum(wrtd_data d);
    int s = 0;
    foreach (d[i]) s += d[i];
    return s;
  endfunction
  wrtd_data data, e;
  int a[];
  int err = 0;
  initial begin
    data = new[8];
    for (int i = 0; i < 8; i++) data[i] = i * 10;
    `CHK(sum(data[1:4]) == 100, "sum of slice")
    a = new[5];
    foreach (a[i]) a[i] = i + 1;
    a = a[1:3];
    `CHK(a.size() == 3 && a[0] == 2 && a[1] == 3 && a[2] == 4, "a = a[1:3]")
    e = data[0:0];
    `CHK(e.size() == 1 && e[0] == 0, "single element slice")
    if (err == 0) $display("PASSED");
  end
endmodule
