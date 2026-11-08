// Expected values: IEEE 1800-2017 11.8.1/11.8.2 (expression type depends only on the operands; width is the maximum of
// the operands and the assignment/return context; an operand is sign-extended only if the propagated type is signed),
// 13.4 (return value context), 10.9.1 (assignment pattern members assigned as in an assignment).
// g7 mixes a signed int and an unsigned operand: the expression is unsigned, so c is zero-extended (4294967293).
typedef longint unsigned uint64_t;
typedef struct { uint64_t a; longint unsigned b; } pair_t;

module svretctx1;
   int c = -3, u = 7;
   const int r = 4096;
   uint64_t a;
   pair_t p;
   uint64_t arr[2];
   int err = 0;
   function automatic void chk(bit ok, string m);
      if (!ok) begin $error("%s", m); err++; end
   endfunction
   function automatic uint64_t g1(); return c * r + u; endfunction
   function automatic uint64_t g2(); return c * r + u * r; endfunction
   function automatic uint64_t g3(); return 5 + c * r + u * r * 125; endfunction
   function automatic uint64_t g4(); return u * r * 125; endfunction
   function automatic uint64_t g5(); return c + u * 3; endfunction
   function automatic uint64_t g6(); return (c + u) * 3; endfunction
   function automatic uint64_t g7(int unsigned uc); return c * r + uc; endfunction
   function automatic longint unsigned g8(); return c; endfunction
   function automatic bit [39:0] g9(); return c * 2; endfunction
   initial begin
      a = g1(); chk(a == 64'd18446744073709539335, "g1");   // 2^64 - 12281
      a = g2(); chk(a == 64'd16384, "g2");
      a = g3(); chk(a == 64'd3571717, "g3");
      a = g4(); chk(a == 64'd3584000, "g4");
      a = g5(); chk(a == 64'd18, "g5");
      a = g6(); chk(a == 64'd12, "g6");
      a = g7(3); chk(a == 64'd17592186032131, "g7");        // (4294967293*4096 + 3) mod 2^64
      a = g8(); chk(a == 64'd18446744073709551613, "g8");   // 2^64 - 3
      chk(g9() === 40'hfffffffffa, "g9");
      a = 5 + c * r + u * r * 125; chk(a == 64'd3571717, "expr1");
      a = c * r * 125 + 1; chk(a == 64'd18446744073708015617, "expr2");   // 2^64 - 1535999
      p = '{c * r + u, c * r + u};
      chk(p.a == 64'd18446744073709539335 && p.b == 64'd18446744073709539335, "pattern");
      p = '{default: c * r + 0};
      chk(p.a == 64'd18446744073709539328 && p.b == 64'd18446744073709539328, "default pattern");   // 2^64 - 12288
      arr = '{c * r + u, c * 2 + 0};
      chk(arr[0] == 64'd18446744073709539335 && arr[1] == 64'd18446744073709551610, "array pattern");   // 2^64 - 6
      if (err == 0) $display("PASSED");
   end
endmodule
