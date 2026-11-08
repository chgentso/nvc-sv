// Self-checking. Expected values: IEEE 1800-2017 11.4.5 (equality of unpacked arrays/queues: same number of
// elements and every element pair equal; class handles compare by identity, strings and reals by value),
// 7.10 (queues), 7.5 (dynamic arrays: arrays of different size are not equal).
`define CHK(tag, got, exp) if ((got) !== (exp)) begin errs++; $error("%s: got %0d expected %0d", tag, got, exp); end
class I; int a; endclass
module svarrcmp1;
  int errs = 0;
  I x, y; I q1[$], q2[$]; real r1[$], r2[$]; string s1[$], s2[$]; string d1[], d2[]; I dh1[], dh2[];
  initial begin
    x = new; y = new;
    q1.push_back(x); q2.push_back(x);
    `CHK("h same", q1 == q2, 1'b1)
    q2[0] = y;
    `CHK("h diff ==", q1 == q2, 1'b0) `CHK("h diff !=", q1 != q2, 1'b1)
    r1 = '{1.5, 2.5}; r2 = '{1.5, 2.5};
    `CHK("r same", r1 == r2, 1'b1)
    r2[1] = 3.0;
    `CHK("r diff", r1 == r2, 1'b0)
    s1 = '{"a", "b"}; s2 = '{"a", "b"};
    `CHK("s same", s1 == s2, 1'b1)
    s2[1] = "c";
    `CHK("s diff", s1 == s2, 1'b0)
    d1 = new[2]; d2 = new[2]; d1[0] = "x"; d2[0] = "x";
    `CHK("d same", d1 == d2, 1'b1)
    d2 = new[3];
    `CHK("d size diff", d1 == d2, 1'b0)
    dh1 = new[1]; dh2 = new[1]; dh1[0] = x; dh2[0] = x;
    `CHK("dh same", dh1 == dh2, 1'b1)
    if (errs == 0) $display("PASSED");
  end
endmodule
