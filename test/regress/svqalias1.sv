// Queues, associative/dynamic/fixed arrays of unpacked structs copy by value, not by reference.
// Expected values: IEEE 1800-2017 7.2 (structure assignment copies members), 7.10 (queues: insert,
// push_back, assignment, concatenation {q, s}, '{} patterns), 7.8/7.5/7.4 (assoc, dynamic, fixed arrays)
typedef struct { int t; int id; } req_t;
module svqalias1;
  req_t q[$], q2[$], q3[$], q4[$], q5[$];
  req_t a[int];
  req_t d[];
  req_t f[2];
  req_t s;
  int errs = 0;
  task automatic chk(string n, req_t x, int t, int id);
    if (x.t !== t || x.id !== id) begin errs++; $error("%s: t=%0d id=%0d expected t=%0d id=%0d", n, x.t, x.id, t, id); end
  endtask
  task automatic size(string n, int got, int exp);
    if (got != exp) begin errs++; $error("%s: size %0d expected %0d", n, got, exp); end
  endtask
  initial begin
    s.t = 0; s.id = 5;
    q.insert(0, s); q.push_back(s);
    q2 = q;                       // queue assign
    q3 = {q, s};                  // concat
    q4 = '{s, s};                 // pattern
    q5.push_back(s); q5[0] = s;   // element assign
    a[3] = s; a[4] = q[0];
    d = new[2]; d[0] = s; d[1] = q[0];
    f[0] = s; f = '{s, s};
    s.t = 9; s.id = 9;
    q[0].t = 7; q5[0].id = 8; d[1].t = 6;
    size("q", q.size(), 2); size("q2", q2.size(), 2); size("q3", q3.size(), 3);
    size("q4", q4.size(), 2); size("q5", q5.size(), 1);
    chk("q0", q[0], 7, 5); chk("q1", q[1], 0, 5);
    chk("q2_0", q2[0], 0, 5); chk("q2_1", q2[1], 0, 5);
    chk("q3_0", q3[0], 0, 5); chk("q3_1", q3[1], 0, 5); chk("q3_2", q3[2], 0, 5);
    chk("q4_0", q4[0], 0, 5); chk("q4_1", q4[1], 0, 5);
    chk("q5_0", q5[0], 0, 8);
    chk("a3", a[3], 0, 5); chk("a4", a[4], 0, 5);
    chk("d0", d[0], 0, 5); chk("d1", d[1], 6, 5);
    chk("f0", f[0], 0, 5); chk("f1", f[1], 0, 5);
    chk("s", s, 9, 9);
    if (errs == 0) $display("PASSED");
  end
endmodule
