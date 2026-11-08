// Continuous assignments whose left-hand side and right-hand side are
// unpacked structures, unpacked arrays of unpacked structures and
// structures with unpacked array members: the right-hand side is
// re-evaluated whenever any part of an operand changes, and the whole
// value, including 4-state members left unassigned, is copied.
// IEEE 1800-2017 10.3 (continuous assignments), 10.3.2 (the continuous
// assignment statement: "whenever an operand in the right-hand side
// changes value, the right-hand side shall be evaluated"), 6.5 (a
// variable may be written by one continuous assignment), 7.2, 7.4, 7.6.

module svunpackcassign1;
   typedef struct {
      logic [7:0] x;
      logic       c;
   } flat_t;

   typedef struct {
      logic       a;
      logic [7:0] b[0:1];
   } nest_t;

   flat_t s_in, s_out;
   flat_t sa_in[0:1], sa_out[0:1];
   nest_t n_in, n_out;
   nest_t na_in[1:0], na_out[1:0];

   assign s_out = s_in;
   assign sa_out = sa_in;
   assign n_out = n_in;
   assign na_out = na_in;

   initial begin
      #1;
      // Nothing assigned yet: all 4-state members are X
      if (s_out.x !== 8'hxx || s_out.c !== 1'bx) $fatal(1, "s_out = %p", s_out);
      if (na_out[1].b[0] !== 8'hxx) $fatal(1, "na_out = %p", na_out);

      s_in.x = 8'h12;
      sa_in[1].x = 8'h34;
      n_in.b[1] = 8'h56;
      na_in[0].b[0] = 8'h78;
      na_in[1].a = 1'b1;
      #1;
      if (s_out.x !== 8'h12 || s_out.c !== 1'bx) $fatal(1, "s_out = %p", s_out);
      if (sa_out[1].x !== 8'h34 || sa_out[0].x !== 8'hxx)
        $fatal(1, "sa_out = %p", sa_out);
      if (n_out.b[1] !== 8'h56 || n_out.b[0] !== 8'hxx || n_out.a !== 1'bx)
        $fatal(1, "n_out = %p", n_out);
      if (na_out[0].b[0] !== 8'h78 || na_out[0].b[1] !== 8'hxx)
        $fatal(1, "na_out[0] = %p", na_out[0]);
      if (na_out[1].a !== 1'b1 || na_out[1].b[1] !== 8'hxx)
        $fatal(1, "na_out[1] = %p", na_out[1]);

      // Whole-value assignment to the right-hand side variable
      s_in = '{x: 8'h9a, c: 1'b0};
      na_in[1] = '{a: 1'b0, b: '{8'hbc, 8'hde}};
      #1;
      if (s_out.x !== 8'h9a || s_out.c !== 1'b0) $fatal(1, "s_out = %p", s_out);
      if (na_out[1].a !== 1'b0 || na_out[1].b[0] !== 8'hbc
          || na_out[1].b[1] !== 8'hde)
        $fatal(1, "na_out[1] = %p", na_out[1]);

      $display("PASSED");
   end
endmodule
