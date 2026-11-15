// A type declared in a specialisation of a parameterised class can be used
// in a declaration or typedef: C #(8)::t (IEEE 1800-2017 8.25.1, 8.18).

class Holder #(parameter int W = 4);
   typedef logic [W-1:0] vec_t;
   typedef struct packed { vec_t hi; vec_t lo; } pair_t;
   vec_t v;
endclass

typedef Holder#(8)::vec_t byte_t;

module vlogclasstype1;
   Holder#(6)::vec_t a;
   Holder #()::pair_t p;
   Holder#(3)::pair_t q [2];
   byte_t b;

   typedef Holder#(5)::vec_t five_t;
   five_t f;

   initial begin
      if ($bits(a) != 6) $fatal(1, "a %0d", $bits(a));
      if ($bits(p) != 8) $fatal(1, "p %0d", $bits(p));
      if ($bits(q[0]) != 6) $fatal(1, "q %0d", $bits(q[0]));
      if ($bits(b) != 8) $fatal(1, "b %0d", $bits(b));
      if ($bits(f) != 5) $fatal(1, "f %0d", $bits(f));
      p.hi = 4'ha;
      p.lo = 4'h5;
      a = 6'h2a;
      if (p !== 8'ha5 || a !== 6'h2a) $fatal(1, "value %h %h", p, a);
      $display("PASSED");
   end
endmodule
