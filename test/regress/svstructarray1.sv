// Expected values: IEEE 1800-2017 7.2 (unpacked structures: members take their default values, int 0 and
// byte 0 for 2-state types), 7.2.1/10.10 (assignment of an unpacked struct copies all members including
// fixed-size array members; assignment patterns), 7.5, 7.10 (elements of dynamic arrays and queues are copies)
// Fixed size array members of an unpacked structure
typedef struct {
   int n;
   int a[3];
   byte b[2];
} s_t;
module svstructarray1;
  s_t s, t;
  s_t arr[];
  s_t q[$];
  int errs = 0;
  task automatic ck(string w, int got, int exp);
    if (got != exp) begin $error("%s: got %0d expected %0d", w, got, exp); errs++; end
  endtask
  initial begin
    ck("init n", s.n, 0); ck("init a1", s.a[1], 0); ck("init b1", s.b[1], 0);
    s.n = 5; s.a[0] = 1; s.a[2] = 3; s.b[1] = 9;
    t = s;
    s.a[2] = 99; s.b[1] = 1;
    ck("t.n", t.n, 5); ck("t.a0", t.a[0], 1); ck("t.a2", t.a[2], 3); ck("t.b1", t.b[1], 9);
    ck("s.a2", s.a[2], 99); ck("s.b1", s.b[1], 1);
    q.push_back(s);
    s.a[0] = 42;
    ck("q0.a0", q[0].a[0], 1); ck("s.a0", s.a[0], 42);
    arr = new[2];
    arr[1] = t;
    t.a[1] = 77;
    ck("arr1.a2", arr[1].a[2], 3); ck("arr1.a1", arr[1].a[1], 0); ck("t.a1", t.a[1], 77);
    arr[0] = '{n: 1, a: '{7, 8, 9}, b: '{1, 2}};
    ck("arr0.n", arr[0].n, 1); ck("arr0.a2", arr[0].a[2], 9); ck("arr0.b1", arr[0].b[1], 2);
    if (errs == 0) $display("PASSED");
  end
endmodule
