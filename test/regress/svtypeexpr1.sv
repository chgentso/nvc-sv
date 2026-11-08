// Expected values: IEEE 1800-2017 11.6 (expression bit lengths), 11.8.1 (rules
// for expression types: width is the maximum of the operands, the result is
// signed only if all operands are signed, 4-state if any operand is 4-state;
// relational/equality/logical/reduction results are 1 bit, unsigned; part
// selects, concatenations and replications are unsigned), 20.6.1 ($typename),
// 6.23 (type operator), 20.6.2 ($bits), 6.24.1 (casting).
// The text printed by $typename for a derived expression type (for example
// whether "i + i" is called "int" or "bit signed[31:0]") is implementation-
// defined, so the type of every expression is checked through its LRM-defined
// properties instead: width ($bits), signedness (the all-ones pattern of a
// variable of type(expr) is negative) and state-ness (an x written to such a
// variable survives).  For the literals and system functions whose 2-/4-state
// nature the LRM leaves open the state check is skipped; real and string
// results are compared with $typename of the declared type.
module svtypeexpr1;
  int i; logic [7:0] v; bit [7:0] bv; byte by; shortint si; longint li; integer ig; time t; real r; string s;
  logic l; bit b; logic signed [7:0] sv; bit signed [3:0] sb; reg [3:0] rg; wire [3:0] w;
  logic [3:0][7:0] pk; int q[$]; int arr[4]; logic [7:0] marr[4]; logic [7:0] aarr [string];
  struct packed { logic [3:0] a; bit [3:0] b; } ps; struct { int x; logic [3:0] y; } us;
  int errs;
  `define FAIL(N, M) begin errs++; $error("case %0d: %s", N, M); end
  `define VEC(N, E, W, S, F) begin \
    var type(E) tv; \
    if ($bits(E) != W) `FAIL(N, $sformatf("width %0d, expected %0d", $bits(E), W)) \
    tv = '1; \
    if (S >= 0 && ((tv < 0) ? 1 : 0) != S) `FAIL(N, $sformatf("signed=%0d, expected %0d", (tv < 0) ? 1 : 0, S)) \
    tv = 'x; \
    if (F >= 0 && ($isunknown(tv) ? 1 : 0) != F) `FAIL(N, $sformatf("4-state=%0d, expected %0d", $isunknown(tv) ? 1 : 0, F)) \
  end
  `define KIND(N, E, T) begin string ge, ex; ge = $typename(E); ex = $typename(T); \
    if (ge != ex) `FAIL(N, {"type ", ge, ", expected ", ex}) end
  initial begin
    `VEC(1, i + i, 32, 1, 0)
    `VEC(2, by + by, 8, 1, 0)
    `VEC(3, by + 1, 32, 1, 0)
    `VEC(4, si * si, 16, 1, 0)
    `VEC(5, li + i, 64, 1, 0)
    `VEC(6, i + li, 64, 1, 0)
    `VEC(7, ig + i, 32, 1, 1)
    `VEC(8, bv + bv, 8, 0, 0)
    `VEC(9, bv + v, 8, 0, 1)
    `VEC(10, v & v, 8, 0, 1)
    `VEC(11, ~v, 8, 0, 1)
    `VEC(12, ~bv, 8, 0, 0)
    `VEC(13, -i, 32, 1, 0)
    `VEC(14, !v, 1, 0, 1)
    `VEC(15, !bv, 1, 0, 0)
    `VEC(16, &v, 1, 0, 1)
    `VEC(17, &bv, 1, 0, 0)
    `VEC(18, v == v, 1, 0, 1)
    `VEC(19, bv == bv, 1, 0, 0)
    `VEC(20, v < 5, 1, 0, 1)
    `VEC(21, v && v, 1, 0, 1)
    `VEC(22, bv || bv, 1, 0, 0)
    `VEC(23, v << 2, 8, 0, 1)
    `VEC(24, bv >> i, 8, 0, 0)
    `VEC(25, l ? v : v, 8, 0, 1)
    `VEC(26, b ? bv : bv, 8, 0, 0)
    `VEC(27, b ? bv : v, 8, 0, 1)
    `VEC(28, b ? i : by, 32, 1, 0)
    `VEC(29, {bv, bv}, 16, 0, 0)
    `VEC(30, {bv, v}, 16, 0, 1)
    `VEC(31, {2{bv}}, 16, 0, 0)
    `VEC(32, {i, by}, 40, 0, 0)
    `VEC(33, v[3], 1, 0, 1)
    `VEC(34, bv[3], 1, 0, 0)
    `VEC(35, bv[5:2], 4, 0, 0)
    `VEC(37, i[3:0], 4, 0, 0)
    `VEC(38, pk[1], 8, 0, 1)
    `VEC(39, pk[1][3], 1, 0, 1)
    `VEC(40, pk[1][3:2], 2, 0, 1)
    `VEC(41, arr[1], 32, 1, 0)
    `VEC(42, marr[1], 8, 0, 1)
    `VEC(43, marr[1][2], 1, 0, 1)
    `VEC(44, aarr["a"], 8, 0, 1)
    `VEC(45, q[1], 32, 1, 0)
    `VEC(46, q.size(), 32, 1, 0)
    `VEC(47, s.len(), 32, 1, 0)
    `VEC(48, ps.a, 4, 0, 1)
    `VEC(49, ps.b, 4, 0, 0)
    `VEC(50, us.x, 32, 1, 0)
    `VEC(51, us.y, 4, 0, 1)
    `VEC(52, ps, 8, 0, 1)
    `VEC(53, sv, 8, 1, 1)
    `VEC(54, sv + sv, 8, 1, 1)
    `VEC(55, sb, 4, 1, 0)
    `VEC(56, sb + sb, 4, 1, 0)
    `VEC(57, sv + 1, 32, 1, 1)
    `VEC(59, rg, 4, 0, 1)
    `VEC(61, 4'b1010, 4, 0, -1)
    `VEC(62, 4'sb1010, 4, 1, -1)
    `VEC(63, '1, 1, 0, -1)
    `VEC(64, 1'b1, 1, 0, -1)
    `VEC(65, 8'hxx, 8, 0, 1)
    `VEC(66, 100, 32, 1, -1)
    `VEC(67, -1, 32, 1, -1)
    `KIND(68, r + r, real)
    `KIND(69, r + i, real)
    `KIND(70, i / 2.0, real)
    `VEC(71, $time, 64, 0, 1)
    `KIND(72, $realtime, real)
    `VEC(73, $random, 32, 1, -1)
    `VEC(74, $urandom, 32, 0, 0)
    `VEC(75, $urandom_range(3), 32, 0, 0)
    `VEC(76, $signed(v), 8, 1, 1)
    `VEC(77, $unsigned(sv), 8, 0, 1)
    `VEC(78, $clog2(i), 32, 1, -1)
    `VEC(79, $bits(v), 32, 1, -1)
    `KIND(80, s, string)
    `KIND(81, {s, s}, string)
    `VEC(82, int'(v), 32, 1, 0)
    begin
      int ci; v = 8'hff; ci = int'(v);
      if (ci != 255) `FAIL(82, "int'(v) of an unsigned 8-bit value should be 255")
    end
    `VEC(83, 8'(i), 8, 1, 0)
    `VEC(84, signed'(bv), 8, 1, 0)
    `VEC(85, t, 64, 0, 1)
    `VEC(86, t + 1, 64, 0, 1)
    `VEC(87, (i), 32, 1, 0)
    `VEC(88, i++, 32, 1, 0)
    `VEC(90, l, 1, 0, 1)
    `VEC(91, l & b, 1, 0, 1)
    `VEC(92, b & b, 1, 0, 0)
    `VEC(93, l + b, 1, 0, 1)
    `VEC(94, i + b, 32, 0, 0)
    `VEC(95, i + l, 32, 0, 1)
    `VEC(96, v + 1, 32, 0, 1)
    `VEC(97, bv + 1, 32, 0, 0)
    `VEC(98, si + 1, 32, 1, 0)
    `VEC(99, i ** 2, 32, 1, 0)
    `VEC(100, by == by, 1, 0, 0)
    if (errs == 0) $display("PASSED");
  end
endmodule
