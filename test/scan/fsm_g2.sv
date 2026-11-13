module g2(input clk, rst, go, abort);
  enum {S0, S1, S2} st, nx;
  always_comb begin
    nx = st;
    case (st)
      S0: if (go) nx = S1;
      S1: nx = S2;
      S2: nx = S0;
    endcase
    if (abort) nx = S0;
  end
  always_ff @(posedge clk) if (rst) st <= S0; else st <= nx;
endmodule

module g2b(input clk_i, rst_ni, go);
  typedef enum logic [1:0] {IDLE, BUSY, HOLD} fsm_e;
  fsm_e state_q, state_d;
  always_comb begin
    state_d = IDLE;
    unique case (state_q)
      IDLE: if (go) state_d = BUSY;
      BUSY: state_d = HOLD;
      default: state_d = IDLE;
    endcase
  end
  `FF(state_q, state_d, IDLE, clk_i, rst_ni)
endmodule

module g2c(input clk, rst, go);
  generate if (1) begin : g
    typedef enum {A, B} t_e;
    t_e s;
    always_ff @(posedge clk) begin
      if (rst) s <= A;
      else case (s) A: if (go) s <= B; B: s <= A; endcase
    end
  end endgenerate
endmodule
