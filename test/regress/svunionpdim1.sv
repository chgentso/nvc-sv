// Packed dimensions after the type identifier of a packed union type, in
// variable declarations, ANSI port declarations and a typedef, with
// element and member selects; a packed union is a packed type and can be
// the element type of a packed array like a packed structure.
// IEEE 1800-2017 7.3.1 (packed unions), 7.4.1 (packed arrays), 6.18
// (user-defined types), A.2.2.1 (data_type ::= type_identifier
// { packed_dimension }).

typedef union packed {
   logic [15:0] raw;
   struct packed {
      logic [7:0] hi;
      logic [7:0] lo;
   } halves;
} svunionpdim1_word;

module svunionpdim1_pass (input  svunionpdim1_word [1:0][3:0] i,
                          output svunionpdim1_word [1:0][3:0] o);
   assign o = i;
endmodule

module svunionpdim1;
   svunionpdim1_word [1:0][3:0] a, b;
   svunionpdim1_word [2:0]      c;

   typedef svunionpdim1_word [1:0] pair_t;
   pair_t p;
   logic [127:0] flat;

   svunionpdim1_pass u (.i(a), .o(b));

   initial begin
      if ($bits(a) != 128) $fatal(1, "$bits(a) = %0d", $bits(a));
      if ($bits(c) != 48) $fatal(1, "$bits(c) = %0d", $bits(c));
      if ($bits(p) != 32) $fatal(1, "$bits(p) = %0d", $bits(p));

      a = '0;
      a[1][2].raw = 16'hbeef;
      a[0][0].halves.lo = 8'h5a;
      c = 48'h1111_2222_3333;
      p[1].halves.hi = 8'h77;
      p[0] = 16'h0102;

      #1;
      if (b[1][2].halves.hi !== 8'hbe) $fatal(1, "b[1][2] = %h", b[1][2]);
      if (b[0][0].raw !== 16'h005a) $fatal(1, "b[0][0] = %h", b[0][0]);
      if (b[1][2] !== a[1][2]) $fatal(1, "b[1][2] != a[1][2]");
      // The array is packed: element [1][2] is at bits 111:96
      flat = b;
      if (flat[111:96] !== 16'hbeef) $fatal(1, "flat = %h", flat);
      if (c[1].raw !== 16'h2222) $fatal(1, "c[1] = %h", c[1]);
      if (p !== 32'h77xx_0102) $fatal(1, "p = %h", p);
      if (p[0].halves.lo !== 8'h02) $fatal(1, "p[0] = %h", p[0]);

      $display("PASSED");
   end
endmodule
