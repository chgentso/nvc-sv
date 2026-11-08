// Expected values: IEEE 1800-2017 6.20.2 (value parameters of struct type, defaults and
// overrides by name and by position; an integral value converts to a packed struct),
// 7.2.2 (packed struct member order: first member in the MSBs), 23.10 (parameter
// override), 27.4 (generate loop with a struct member bound), 25.3 (interface with a
// parameter), 11.4.3 (arithmetic), 20.6.2 ($bits)
typedef struct {
   int num;
   int width;
} t_params;

typedef struct packed {
   logic [3:0] a;
   logic [3:0] b;
} t_pk;

localparam t_params DEFAULT_PARAMS = '{num: 3, width: 6};

interface IPort #(parameter t_params P = '{4, 8})(input logic clk);
   logic [P.width-1:0] d;
endinterface

module leaf #(parameter t_params P = DEFAULT_PARAMS, parameter t_pk K = '{a: 4'h1, b: 4'h2})
   (output logic [P.width-1:0] q);
   assign q = K.a + K.b + P.num;
endmodule

module svstructparam1;
   logic clk = 0;
   int errs;
   int ngen;
   IPort p(clk);
   IPort #(.P('{2, 12})) p2(clk);
   logic [5:0] q1;
   logic [11:0] q2;
   leaf l1(.q(q1));
   leaf #(.P('{5, 12}), .K(8'h34)) l2(.q(q2));
   logic [8:0] q3;
   leaf #('{6, 9}, 8'h55) l3(.q(q3));
   localparam t_params LP = '{num: 7, width: 2};
   for (genvar i = 0; i < LP.num - 5; i++) begin : g
      initial begin
         ngen++;
         if (i < 0 || i > 1) begin $error("genvar %0d", i); errs++; end
      end
   end
   task automatic ck(string tag, int got, int exp);
      if (got !== exp) begin $error("%s: got %0d expected %0d", tag, got, exp); errs++; end
   endtask
   initial begin
      p.d = 8'hA5;
      p2.d = 12'hABC;
      #2;
      ck("ngen", ngen, 2);
      ck("p width", $bits(p.d), 8);
      ck("p d", p.d, 8'hA5);
      ck("p num", p.P.num, 4);
      ck("p2 width", $bits(p2.d), 12);
      ck("p2 d", p2.d, 12'hABC);
      ck("p2 num", p2.P.num, 2);
      ck("l1 width", l1.P.width, 6);
      ck("l1 num", l1.P.num, 3);
      ck("l1 K", l1.K, 8'h12);
      ck("q1", q1, 6);
      ck("l2 width", l2.P.width, 12);
      ck("l2 num", l2.P.num, 5);
      ck("l2 K", l2.K, 8'h34);
      ck("q2", q2, 12);
      ck("l3 width", l3.P.width, 9);
      ck("l3 num", l3.P.num, 6);
      ck("l3 K", l3.K, 8'h55);
      ck("q3", q3, 16);
      ck("lp", LP.width, 2);
      if (errs == 0) $display("PASSED");
   end
endmodule
