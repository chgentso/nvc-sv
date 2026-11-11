// std::randomize of a variable of an enumerated type only produces the
// values of its names
module svrandenum2;
   typedef enum { A, B, C, D, E } plain_t;
   typedef enum bit [3:0] { P = 1, Q = 4, R = 7, S = 12 } sparse_t;

   initial begin
      plain_t p;
      sparse_t s;
      int seen_p[plain_t];
      int seen_s[sparse_t];

      repeat (300) begin
         if (!std::randomize(p) with { p != C; }) $fatal(1, "plain failed");
         if (!(p inside {A, B, D, E})) $fatal(1, "plain value %0d", p);
         seen_p[p]++;

         if (!std::randomize(s)) $fatal(1, "sparse failed");
         if (!(s inside {P, Q, R, S})) $fatal(1, "sparse value %0d", s);
         seen_s[s]++;
      end

      if (seen_p.num() != 4) $fatal(1, "plain coverage %0d", seen_p.num());
      if (seen_s.num() != 4) $fatal(1, "sparse coverage %0d", seen_s.num());
      $display("PASSED");
   end
endmodule
