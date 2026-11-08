// Member initial values given in an unpacked structure typedef for an
// unpacked array member, with an assignment pattern: every variable of
// the type, including each element of an unpacked array of the
// structure, starts with the pattern value, and reads of the elements
// return it.
// IEEE 1800-2017 7.2.2 (assigning to structures: "members of unpacked
// structures may be given an initial value"), 10.9.1 (array assignment
// patterns), 6.8.

module svunpackdflt1;
   typedef struct {
      logic [3:0] f[2] = '{4'h5, 4'ha};
      int         g = 7;
   } dflt_t;

   dflt_t s;
   dflt_t df[2];

   initial begin
      if (s.f[0] !== 4'h5 || s.f[1] !== 4'ha || s.g != 7)
        $fatal(1, "s = %p", s);
      foreach (df[j])
        if (df[j].f[0] !== 4'h5 || df[j].f[1] !== 4'ha || df[j].g != 7)
          $fatal(1, "df[%0d] = %p", j, df[j]);

      df[1].f[1] = 4'h0;
      if (df[0].f[1] !== 4'ha || df[1].f[0] !== 4'h5)
        $fatal(1, "df = %p", df);

      $display("PASSED");
   end
endmodule
