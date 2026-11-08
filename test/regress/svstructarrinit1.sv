// Expected values: IEEE 1800-2017 7.2.2 (packed structure layout: first member is
// the MSB), 6.19 (enum), 10.9.2 (assignment patterns by member name), 6.20 (parameters),
// 11.4.12 (concatenation of the packed value as a vector)
package pk;
  typedef enum logic [1:0] { OFF = 0, TOR = 1 } mode_e;
  typedef struct packed {
    logic lock;
    mode_e mode;
    logic exec;
  } cfg_t;
  parameter int N = 3;
  parameter cfg_t Rst[N] = '{
    '{lock: 1'b1, mode: TOR, exec: 1'b0},
    '{lock: 1'b0, mode: OFF, exec: 1'b1},
    '{lock: 1'b0, mode: TOR, exec: 1'b1}
  };
endpackage
module svstructarrinit1;
  import pk::*;
  localparam cfg_t L[2] = '{'{lock: 1'b1, mode: OFF, exec: 1'b1}, '{lock: 1'b0, mode: TOR, exec: 1'b0}};
  cfg_t v[2] = '{'{lock: 1'b0, mode: TOR, exec: 1'b1}, '{lock: 1'b1, mode: OFF, exec: 1'b0}};
  int errs;
  initial begin
    if (Rst[0] !== 4'b1010) begin $error("Rst[0] %b", Rst[0]); errs++; end
    if (Rst[1] !== 4'b0001) begin $error("Rst[1] %b", Rst[1]); errs++; end
    if (Rst[2] !== 4'b0011) begin $error("Rst[2] %b", Rst[2]); errs++; end
    if (L[0] !== 4'b1001) begin $error("L[0] %b", L[0]); errs++; end
    if (L[1] !== 4'b0010) begin $error("L[1] %b", L[1]); errs++; end
    if (v[0] !== 4'b0011) begin $error("v[0] %b", v[0]); errs++; end
    if (v[1] !== 4'b1000) begin $error("v[1] %b", v[1]); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
