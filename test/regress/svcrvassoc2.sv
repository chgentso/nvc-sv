// Self-checking test of associative arrays with foreach constraints (string and int keys, sum() in a
// constraint, constraints reading other arrays) and of the array reduction/locator methods on associative
// arrays.
// Expected values: IEEE 1800-2017 18.5.8 (foreach constraints, array reduction methods in constraints), 7.12.3 (reduction methods sum, product, and, or, xor; element-width
// results), 7.12.1 (min, max, unique), 7.12.2 and 7.12.4 (`with` clause, item.index = the key), 7.8.
// Hand-derived values: a = {3, 5, -2}: sum 6, product -30, min -2, max 5; q = {5:4, -1:6, 9:1}: sum 11,
// product 24, min 1, max 6, sum with (item*2) = 22, sum of the keys = 13, 3 distinct values;
// b = {0f, f3, 35}: and 01, or ff, xor c9 (8-bit results).
module svcrvassoc2;
  class c;
    rand int m[string];
    rand bit [7:0] n[int];
    rand int w[int];
    rand int u[string];
    int sel[int];
    int lbl[string];
    constraint cm { foreach (m[k]) m[k] inside {[1:5]}; m.sum() == 9; }
    constraint cn { foreach (n[i]) n[i] > i; }
    constraint cw { foreach (w[i]) w[i] == sel[i] + 1; }
    constraint cu { foreach (u[s]) u[s] == lbl[s] * 2; }
  endclass

  initial begin
    automatic c o = new;
    automatic int bad = 0, range = 0, fails = 0;
    bit [7:0] r8;
    int a[string];
    int q[int];
    bit [7:0] b[int];
    o.m["a"] = 0; o.m["bb"] = 0; o.m["c"] = 0;
    o.n[3] = 0; o.n[10] = 0;
    o.w[1] = 0; o.w[2] = 0; o.sel[1] = 10; o.sel[2] = 20;
    o.u["x"] = 0; o.u["yy"] = 0; o.lbl["x"] = 7; o.lbl["yy"] = 11; o.lbl["zz"] = 1;
    repeat (200) begin
      if (!o.randomize()) bad++;
      else begin
        if (o.m.sum() != 9) range++;
        foreach (o.m[k]) if (o.m[k] < 1 || o.m[k] > 5) range++;
        if (o.n[3] <= 3 || o.n[10] <= 10) range++;
        if (o.w[1] != 11 || o.w[2] != 21) range++;
        if (o.u["x"] != 14 || o.u["yy"] != 22) range++;
      end
    end
    if (bad != 0 || range != 0) begin fails++; $error("randomize: bad=%0d range=%0d", bad, range); end

    a["one"] = 3; a["two"] = 5; a["three"] = -2;
    q[5] = 4; q[-1] = 6; q[9] = 1;
    b[1] = 8'h0f; b[2] = 8'hf3; b[7] = 8'h35;
    if (a.sum() != 6) begin fails++; $error("a.sum() = %0d", a.sum()); end
    if (a.product() != -30) begin fails++; $error("a.product() = %0d", a.product()); end
    if (q.sum() != 11) begin fails++; $error("q.sum() = %0d", q.sum()); end
    if (q.product() != 24) begin fails++; $error("q.product() = %0d", q.product()); end
    r8 = b.and(); if (r8 !== 8'h01) begin fails++; $error("b.and() = %h", r8); end
    r8 = b.or();  if (r8 !== 8'hff) begin fails++; $error("b.or() = %h", r8); end
    r8 = b.xor(); if (r8 !== 8'hc9) begin fails++; $error("b.xor() = %h", r8); end
    if (q.min()[0] != 1 || q.max()[0] != 6) begin fails++; $error("q.min/max = %0d %0d", q.min()[0], q.max()[0]); end
    if (a.min()[0] != -2 || a.max()[0] != 5) begin fails++; $error("a.min/max = %0d %0d", a.min()[0], a.max()[0]); end
    if ((q.sum() with (item * 2)) != 22) begin fails++; $error("q.sum with item*2 = %0d", q.sum() with (item * 2)); end
    if ((q.sum() with (item.index)) != 13) begin fails++; $error("q.sum with item.index = %0d", q.sum() with (item.index)); end
    if (q.unique().size() != 3) begin fails++; $error("q.unique().size() = %0d", q.unique().size()); end
    if (fails == 0) $display("PASSED");
  end
endmodule
