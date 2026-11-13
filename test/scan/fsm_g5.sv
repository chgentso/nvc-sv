// A machine whose case comes from an included file is skipped
module g5a(input clk);
  typedef enum {P0, P1} t_e;
  t_e st;
  always_ff @(posedge clk) begin
`include "inc/fsm_case.svh"
  end
endmodule

// Errors before, inside and after a machine
module g5b(input clk, rst, go);
  typedef enum {A, B, C} t_e;
  t_e st;
  42 nonsense ;
  always_ff @(posedge clk) begin
    if (rst) st <= A;
    else case (st)
      A: if (go) st <= B;
      B: st <= %%% ;
      C: st <= A;
    endcase
  end
  wire w = ;
endmodule
