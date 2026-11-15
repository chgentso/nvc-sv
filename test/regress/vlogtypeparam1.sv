// Fields of an object whose type is a type parameter are found once the
// parameter is bound for the instance (IEEE 1800-2017 6.20.3, 7.2).

typedef struct packed { logic [7:0] a; logic [7:0] b; } s_t;
typedef struct packed { logic [1:0][7:0] w; logic [3:0] n; } u_t;

module leaf #(parameter type T = logic, parameter T P = '0) (output logic [7:0] o);
   T v;
   logic [7:0] r1, r2;
   initial begin
      v = P;
      r1 = v.b;
      r2 = P.a;
   end
   assign o = r1 ^ r2;
endmodule

module mid #(parameter type T = logic, parameter T P = '0) (output logic [7:0] o);
   leaf #(.T(T), .P(P)) l (.o(o));
endmodule

module wide #(parameter type T = logic, parameter T P = '0) ();
   logic [7:0] w0, w1;
   logic [3:0] n;
   initial begin
      w0 = P.w[0];
      w1 = P.w[1];
      n = P.n;
   end
endmodule

module vlogtypeparam1;
   logic [7:0] o1, o2;
   leaf #(.T(s_t), .P(16'h1234)) l1 (.o(o1));
   mid #(.T(s_t), .P(16'h00ff)) m1 (.o(o2));
   wide #(.T(u_t), .P(20'hab_cd5)) w ();

   initial begin
      #1;
      if (o1 !== 8'h26) $fatal(1, "o1 %h", o1);
      if (o2 !== 8'hff) $fatal(1, "o2 %h", o2);
      if (w.w0 !== 8'hcd || w.w1 !== 8'hab || w.n !== 4'h5)
         $fatal(1, "w %h %h %h", w.w0, w.w1, w.n);
      $display("PASSED");
   end
endmodule
