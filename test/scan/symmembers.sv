package sm_pkg;
  typedef enum logic [1:0] { SM_A, SM_B = 2 } sm_e_t;
  typedef struct packed { logic [3:0] x; logic y; } sm_s_t;
  enum { SM_ANON } sm_v;
endpackage

interface sm_if;
  logic d;
  modport mp(input d);
endinterface

module sm_top #(parameter int N = 2) (
  input logic clk_i,
  input other_pkg::cfg_t [N-1:0] cfg_i,
  input logic rst_ni,
  output logic [3:0] data_o,
  sm_if.mp bi,
  sm_if other
);
endmodule
