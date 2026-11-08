// Unpacked arrays with a lower bound other than zero declared with a typedef
// Expected values: IEEE 1800-2017 7.4.2, 10.9.1 (assignment patterns: positional, index:value, default),
// 7.12 (array locator, ordering and reduction methods; index returns the declared index),
// 7.4.5 (array slices), 10.10 (unpacked array concatenation), 11.4.5 (array equality), 20.7
typedef int t_a [1:3];
typedef int t_d [3:1];
typedef struct { int a; t_a arr; } st_t;
typedef struct { int a; int arr[2:4]; } st2_t;
`define CHK(c, m) if (!(c)) begin errs++; $error(m); end
class C;
  t_a ca;
  int pa [5:6];
endclass
module svlobound2;
  t_a t, u; t_d dd; st_t s; st2_t s2;
  int idx[$];
  int q[$];
  int errs = 0;
  initial begin
    C c;
    t = '{1:5, 2:6, 3:7};
    `CHK(t[1] == 5 && t[2] == 6 && t[3] == 7, "A")
    t = '{default:4, 2:9};
    `CHK(t[1] == 4 && t[2] == 9 && t[3] == 4, "B")
    dd = '{3:1, 2:2, 1:3};
    `CHK(dd[1] == 3 && dd[2] == 2 && dd[3] == 1, "C")
    u = t;
    `CHK(u[1] == 4 && u[2] == 9 && u[3] == 4, "D")
    s = '{a:1, arr:'{10,20,30}};
    `CHK(s.a == 1 && s.arr[1] == 10 && s.arr[2] == 20 && s.arr[3] == 30, "E")
    s2 = '{a:1, arr:'{10,20,30}};
    `CHK(s2.a == 1 && s2.arr[2] == 10 && s2.arr[3] == 20 && s2.arr[4] == 30, "F")
    `CHK($low(s.arr) == 1 && $high(s2.arr) == 4, "G")
    c = new; c.ca = '{4,5,6}; c.pa = '{7,8};
    `CHK(c.ca[1] == 4 && c.ca[3] == 6 && c.pa[5] == 7 && c.pa[6] == 8 && $low(c.ca) == 1, "H")
    idx = t.find_index(x) with (x == 9);
    `CHK(idx.size() == 1 && idx[0] == 2, "I")
    q = t.find(x) with (x > 4);
    `CHK(q.size() == 1 && q[0] == 9, "J")
    t.reverse();
    `CHK(t[1] == 4 && t[2] == 9 && t[3] == 4, "K")
    t.sort();
    `CHK(t[1] == 4 && t[2] == 4 && t[3] == 9, "L")
    `CHK(t.sum() == 17 && t.product() == 144, "M")
    t[1:2] = '{100, 200};
    `CHK(t[1] == 100 && t[2] == 200 && t[3] == 9, "N")
    q = {t[1:2]};
    `CHK(q.size() == 2 && q[0] == 100 && q[1] == 200, "O")
    u = '{default:0}; u[2] = 1;
    `CHK(u[1] == 0 && u[2] == 1 && u[3] == 0, "P")
    `CHK((t == u) == 0, "Q")
    t = '{7,8,9};
    `CHK(t[3-1] == 8, "R")
    if (errs == 0) $display("PASSED");
  end
endmodule
