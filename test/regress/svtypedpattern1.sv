// Expected values: IEEE 1800-2017 10.9.1/10.9.2 (typed assignment patterns for structures, arrays and queues),
// 7.10 (queues), 7.8 (associative arrays), 7.2.1 (packed struct layout), 6.18 (typedef), 26 (package scope)
package pk;
  typedef struct packed { bit [3:0] a; bit [3:0] b; } pair_t;
  typedef int q_t[$];
endpackage
module svtypedpattern1;
  typedef struct { int x; string s; } rec_t;
  typedef int arr_t[3];
  typedef struct packed { bit f; bit g; int d; } e_t;
  e_t q[string][$];
  rec_t r;
  arr_t a;
  pk::q_t iq;
  pk::pair_t p;
  int errs;
  function automatic int sum(arr_t v); return v[0] + v[1] + v[2]; endfunction
  function automatic int dsum(e_t v); return v.d; endfunction
  initial begin
    q["k"].push_back(e_t'{1, 0, 5});
    q["k"].push_back(e_t'{f: 0, g: 1, default: 7});
    if (q["k"].size() != 2 || q["k"][1].d != 7 || q["k"][0].d != 5 || q["k"][0].f !== 1'b1 || q["k"][1].g !== 1'b1) begin $error("queue of structs"); errs++; end
    r = rec_t'{x: 3, s: "hi"};
    if (r.x != 3 || r.s != "hi") begin $error("rec"); errs++; end
    a = arr_t'{1, 2, 3};
    if (sum(arr_t'{4, 5, 6}) != 15) begin $error("sum"); errs++; end
    iq = pk::q_t'{7, 8, 9};
    if (iq.size() != 3 || iq[2] != 9) begin $error("iq"); errs++; end
    p = pk::pair_t'{4'h3, 4'hc};
    if (p !== 8'h3c) begin $error("pair %h", p); errs++; end
    if (dsum(e_t'{0, 0, 42}) != 42) begin $error("dsum"); errs++; end
    if (a[2] != 3) begin $error("a[2]"); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
