// Not state machines
module g4a(input [1:0] op, input clk);
  typedef enum {X, Y} t_e;
  t_e o;
  always_comb case (o) X: o = Y; Y: o = X; endcase
endmodule

module g4b(input clk);
  typedef enum {P, Q} t_e;
  t_e r;
  always_ff @(posedge clk) r <= Q;
endmodule

module g4c(input clk, input [1:0] m, output reg y);
  localparam K0 = 0, K1 = 1;
  reg k;
  always @(posedge clk) begin
    case (k)
      K0: k <= K1;
      K1: k <= K0;
    endcase
  end
endmodule

module g4d(input clk, input [1:0] sel);
  typedef enum {E0, E1} t_e;
  t_e nxt, cur;
  always_comb case (nxt) E0: cur = E1; E1: cur = E0; endcase
  always_ff @(posedge clk) cur <= cur;
endmodule
