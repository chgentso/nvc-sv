// Enumeration literals, parameters and localparams of an instance reached by
// hierarchical name (IEEE 1800-2017 23.6, 23.7).
module sub #(parameter int P = 3);
  typedef enum logic [1:0] {CmIdle, CmBusy = 2, CmDone} st_e;
  localparam int LP = P + 4;
  enum {RED, GREEN} col = GREEN;
  st_e s = CmBusy;
endmodule

module svhierenum1;
  sub #(.P(5)) si();
  logic [1:0] a;
  initial begin
    a = si.CmIdle;
    $display("%0d %0d %0d %0d %0d", a, si.CmBusy, si.CmDone, si.LP, si.P);
    if (si.s == si.CmBusy) $display("busy");
    if (si.col == si.GREEN) $display("green %0d", si.RED);
    case (si.s)
      si.CmIdle: $display("idle");
      si.CmBusy: $display("case busy");
      default: $display("other");
    endcase
  end
endmodule
