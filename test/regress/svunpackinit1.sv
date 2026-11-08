// Default initial value of every element of an unpacked array member of
// an unpacked structure: X for 4-state element types and 0 for 2-state
// element types, for a structure variable, an unpacked array of such
// structures and a structure nested in a structure.
// IEEE 1800-2017 6.8 (Table 6-7, default variable initial values),
// 7.2 (structures), 7.4 (unpacked arrays).

module svunpackinit1;
   typedef struct {
      logic       a;
      logic [7:0] b[0:2];
      bit [3:0]   c[2];
      int         d[1:0];
   } nest_t;

   typedef struct {
      nest_t      inner;
      logic [1:0] e[3];
   } outer_t;

   nest_t  s;
   nest_t  sa[0:1];
   outer_t o;

   initial begin
      if (s.a !== 1'bx) $fatal(1, "s.a = %b", s.a);
      foreach (s.b[i])
        if (s.b[i] !== 8'hxx) $fatal(1, "s.b[%0d] = %h", i, s.b[i]);
      foreach (s.c[i])
        if (s.c[i] !== 4'h0) $fatal(1, "s.c[%0d] = %h", i, s.c[i]);
      foreach (s.d[i])
        if (s.d[i] !== 0) $fatal(1, "s.d[%0d] = %0d", i, s.d[i]);

      foreach (sa[j]) begin
         foreach (sa[j].b[i])
           if (sa[j].b[i] !== 8'hxx)
             $fatal(1, "sa[%0d].b[%0d] = %h", j, i, sa[j].b[i]);
         foreach (sa[j].c[i])
           if (sa[j].c[i] !== 4'h0)
             $fatal(1, "sa[%0d].c[%0d] = %h", j, i, sa[j].c[i]);
      end

      foreach (o.inner.b[i])
        if (o.inner.b[i] !== 8'hxx)
          $fatal(1, "o.inner.b[%0d] = %h", i, o.inner.b[i]);
      foreach (o.e[i])
        if (o.e[i] !== 2'bxx) $fatal(1, "o.e[%0d] = %b", i, o.e[i]);

      // Writing one element leaves the others unchanged
      sa[1].b[0] = 8'h11;
      if (sa[1].b[1] !== 8'hxx || sa[1].b[2] !== 8'hxx || sa[0].b[0] !== 8'hxx)
        $fatal(1, "sa = %p", sa);

      $display("PASSED");
   end
endmodule
