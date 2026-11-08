// Assignment patterns with nested structures
// Expected values: IEEE 1800-2017 10.9.2 (structure assignment patterns: positional, by member name, default:
// applied to every member not named, also to the members of nested structures), 10.10 (queues of structs), 7.10
module svnestpat1;
  typedef struct { int b; int d; } in_t;
  typedef struct { in_t a; int c; } out_t;
  typedef struct { in_t a[2]; int e; } arr_t;
  out_t o, p;
  in_t q[$];
  int fails = 0;
  task automatic chk(input string what, input int g1, input int g2, input int g3, input int e1, input int e2, input int e3);
    if (g1 != e1 || g2 != e2 || g3 != e3) begin $error("%s: got %0d %0d %0d", what, g1, g2, g3); fails++; end
  endtask
  initial begin
    o = '{a: '{b: 1, d: 2}, c: 3};
    chk("named", o.a.b, o.a.d, o.c, 1, 2, 3);
    p = '{default: 7};
    chk("default", p.a.b, p.a.d, p.c, 7, 7, 7);
    o = '{a: '{default: 4}, c: 5};
    chk("nested default", o.a.b, o.a.d, o.c, 4, 4, 5);
    o = '{'{8, 9}, 10};
    chk("positional", o.a.b, o.a.d, o.c, 8, 9, 10);
    q.push_back('{b: 1, d: 2});
    if (q[0].d != 2 || q[0].b != 1) begin $error("queue element: %0d %0d", q[0].b, q[0].d); fails++; end
    if (fails == 0) $display("PASSED");
  end
endmodule
