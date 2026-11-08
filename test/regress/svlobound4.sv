// Unpacked arrays with a lower bound other than zero declared with a typedef
// Expected values: IEEE 1800-2017 7.4.5 (slices of unpacked arrays), 7.5 (dynamic array from an array),
// 10.9.1 (assignment patterns), 10.10 (unpacked array concatenation), 7.10 (queues)
typedef int t_a [1:4];
`define CHK(c, m) if (!(c)) begin errs++; $error(m); end
module svlobound4;
  int p [1:4] = '{10,20,30,40};
  t_a t = '{10,20,30,40};
  int q[$];
  int da[];
  int errs = 0;
  initial begin
    q = {p[2:3]};
    `CHK(q.size() == 2 && q[0] == 20 && q[1] == 30, "p")
    q = {t[2:3]};
    `CHK(q.size() == 2 && q[0] == 20 && q[1] == 30, "t")
    q = {t[2:3], 5};
    `CHK(q.size() == 3 && q[0] == 20 && q[1] == 30 && q[2] == 5, "t2")
    q = {t};
    `CHK(q.size() == 4 && q[3] == 40, "t3")
    da = t;
    `CHK(da.size() == 4 && da[3] == 40, "t4")
    da = t[2:4];
    `CHK(da.size() == 3 && da[2] == 40, "t5")
    t[3:4] = t[1:2];
    `CHK(t[1] == 10 && t[2] == 20 && t[3] == 10 && t[4] == 20, "t6")
    t[2:3] = '{7, 8};
    `CHK(t[1] == 10 && t[2] == 7 && t[3] == 8 && t[4] == 20, "t7")
    p[2:3] = '{7, 8};
    `CHK(p[1] == 10 && p[2] == 7 && p[3] == 8 && p[4] == 40, "p7")
    t[1:2] = {5, 6};
    `CHK(t[1] == 5 && t[2] == 6 && t[3] == 8 && t[4] == 20, "t8")
    if (errs == 0) $display("PASSED");
  end
endmodule
