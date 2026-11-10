// Self-checking. Expected values: IEEE 1800-2017 23.2.2.3 (rules for
// determining port kind, data type and direction: a port with no direction
// takes that of the previous port; a data type name, imported or scoped, can
// start the declaration of such a port) and 23.2.2.1 (net port type).
package svportdt1_pkg;
  typedef struct packed { logic valid; logic [3:0] code; } err_t;
endpackage

module svportdt1_sub
  import svportdt1_pkg::*;
(
  input  logic clk,
  output logic done,
  err_t err_o,
  svportdt1_pkg::err_t err2_o
);
  assign done = 1'b1;
  assign err_o = '{valid: 1'b1, code: 4'd3};
  assign err2_o = '{valid: 1'b0, code: 4'd9};
endmodule

module svportdt1;
  svportdt1_pkg::err_t e, e2;
  logic d;
  int errs = 0;
  svportdt1_sub u(.clk(1'b0), .done(d), .err_o(e), .err2_o(e2));
  initial begin
    #1;
    if (d !== 1'b1 || e.valid !== 1'b1 || e.code !== 4'd3) begin errs++; $error("e = %b %0d", e.valid, e.code); end
    if (e2.valid !== 1'b0 || e2.code !== 4'd9) begin errs++; $error("e2 = %b %0d", e2.valid, e2.code); end
    if (errs == 0) $display("PASSED");
  end
endmodule
