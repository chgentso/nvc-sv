// Self-checking. Expected values: IEEE 1800-2017 7.4 (arrays of queues / dynamic arrays / fixed arrays as class
// members and module variables), 7.10.2 (push_back, assignment copies a queue), 10.10 (unpacked array
// concatenation, queue-of-queue elements), 5.11 (assignment patterns), 21.2.1.7 (%p prints an assignment pattern:
// the check removes all white space and compares the elements, nothing else about the layout).
`define CHK(tag, got, exp) if ((got) !== (exp)) begin errs++; $error("%s: got %0d expected %0d", tag, got, exp); end
`define CHKS(tag, got, exp) if ((got) != (exp)) begin errs++; $error("%s: got '%s' expected '%s'", tag, got, exp); end
typedef int iq_t[$];
class C;
  int a[4];
  int q[3][$];
  iq_t tq[2];
  int dyn[2][];
  string sq[2][$];
  function void f();
    q[1].push_back(4);
    q[1].push_back(5);
    tq[0].push_back(8);
    tq[1] = {1,2,3};
    dyn[0] = new[2];
    dyn[1] = '{7,8,9};
    sq[1].push_back("hi");
    a[2] = 6;
  endfunction
endclass

module svarrq1;
  int errs = 0;
  C c = new;
  iq_t aq[string];
  int mq[$][$];
  int q[$];
  int s;
  // %p text with all white space removed
  function automatic string nows(string t);
    string r = "";
    for (int i = 0; i < t.len(); i++) if (!(t[i] == " " || t[i] == "\n" || t[i] == "\t")) r = {r, string'(t[i])};
    return r;
  endfunction
  initial begin
    c.f();
    `CHK("q[1].size", c.q[1].size(), 2) `CHK("tq[0].size", c.tq[0].size(), 1) `CHK("tq[1].size", c.tq[1].size(), 3)
    `CHK("dyn[0].size", c.dyn[0].size(), 2) `CHK("dyn[1].size", c.dyn[1].size(), 3)
    `CHKS("sq[1][0]", c.sq[1][0], "hi") `CHK("dyn[1][2]", c.dyn[1][2], 9) `CHK("q[1][1]", c.q[1][1], 5)
    c.q[0] = c.q[1];
    c.q[2] = {9};
    c.dyn[0][1] = 3;
    `CHK("q[0].size", c.q[0].size(), 2) `CHK("q[0][0]", c.q[0][0], 4) `CHK("q[0][1]", c.q[0][1], 5)
    `CHK("q[1].size still", c.q[1].size(), 2)
    `CHK("q[2].size", c.q[2].size(), 1) `CHK("q[2][0]", c.q[2][0], 9)
    `CHK("dyn[0][0]", c.dyn[0][0], 0) `CHK("dyn[0][1]", c.dyn[0][1], 3)
    `CHK("tq[0][0]", c.tq[0][0], 8) `CHK("tq[1][2]", c.tq[1][2], 3)
    `CHK("a[2]", c.a[2], 6) `CHK("a[0]", c.a[0], 0)
    `CHK("sq[0].size", c.sq[0].size(), 0) `CHK("sq[1].size", c.sq[1].size(), 1)
    `CHKS("%p q", nows($sformatf("%p", c.q)), "'{'{4,5},'{4,5},'{9}}")
    `CHKS("%p tq", nows($sformatf("%p", c.tq)), "'{'{8},'{1,2,3}}")
    `CHKS("%p dyn", nows($sformatf("%p", c.dyn)), "'{'{0,3},'{7,8,9}}")
    `CHKS("%p a", nows($sformatf("%p", c.a)), "'{0,0,6,0}")
    `CHKS("%p sq", nows($sformatf("%p", c.sq)), "'{'{},'{\"hi\"}}")
    aq["x"].push_back(3);
    aq["x"].push_back(4);
    `CHK("aq size", aq["x"].size(), 2) `CHK("aq[1]", aq["x"][1], 4)
    s = 4;
    q = {3};
    `CHK("q={3} size", q.size(), 1) `CHK("q={3}", q[0], 3)
    q = {s};
    `CHK("q={s} size", q.size(), 1) `CHK("q={s}", q[0], 4)
    q = {q, 7};
    `CHK("q={q,7} size", q.size(), 2) `CHK("q={q,7}[0]", q[0], 4) `CHK("q={q,7}[1]", q[1], 7)
    mq.push_back({s});
    mq.push_back({5});
    mq.push_back(q);
    mq.push_front({6});
    mq[1] = {8};
    mq[0].push_back(1);
    `CHK("mq.size", mq.size(), 4)
    `CHK("mq[0].size", mq[0].size(), 2) `CHK("mq[0][0]", mq[0][0], 6) `CHK("mq[0][1]", mq[0][1], 1)
    `CHK("mq[1].size", mq[1].size(), 1) `CHK("mq[1][0]", mq[1][0], 8)
    `CHK("mq[2].size", mq[2].size(), 1) `CHK("mq[2][0]", mq[2][0], 5)
    `CHK("mq[3].size", mq[3].size(), 2) `CHK("mq[3][0]", mq[3][0], 4) `CHK("mq[3][1]", mq[3][1], 7)
    `CHKS("%p mq", nows($sformatf("%p", mq)), "'{'{6,1},'{8},'{5},'{4,7}}")
    if (errs == 0) $display("PASSED");
  end
endmodule
