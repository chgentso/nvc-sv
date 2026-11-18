// Forcing the actual of a variable output port does not change the variable inside
// Expected values: IEEE 1800-2017 23.3.3 (an output port of variable type is a continuous assignment from the
// variable to the actual, so the actual is a separate object), 10.6.2 (a force overrides the value of the forced
// object, and a variable or net driven by a continuous assignment returns to the driven value on release); the
// actual of an output port of net type is the same net as the port, so the force is seen inside.
`timescale 1ns/1ns
typedef struct packed { logic p; logic n; } svforceport1_t;
module svforceport1_sub(output logic o, output logic [1:0] ov, output wire [1:0] ow,
                        output svforceport1_t os, output svforceport1_t oa);
  assign o = 1'b1;
  assign ov = 2'b01;
  assign ow = 2'b01;
  assign os.p = 1'b0;
  assign os.n = 1'b1;
  always_comb oa = 2'b01;
endmodule
module svforceport1;
  logic o;
  logic [1:0] ov;
  wire [1:0] ow;
  svforceport1_t os, oa;
  int errs = 0;
  svforceport1_sub u(o, ov, ow, os, oa);
  initial begin
    #1;
    force o = 1'b0; force ov = 2'b11; force ow = 2'b11; force os.n = 1'b0; force oa.n = 1'b0;
    #1;
    if (o !== 1'b0 || ov !== 2'b11 || ow !== 2'b11 || os !== 2'b00 || oa !== 2'b00) begin
      $error("outside while forced: %b %b %b %b %b", o, ov, ow, os, oa); errs++;
    end
    if (u.o !== 1'b1) begin $error("inside o forced: %b", u.o); errs++; end
    if (u.ov !== 2'b01) begin $error("inside ov forced: %b", u.ov); errs++; end
    if (u.ow !== 2'b11) begin $error("inside ow must see the force: %b", u.ow); errs++; end
    if (u.os !== 2'b01) begin $error("inside os forced: %b", u.os); errs++; end
    if (u.oa !== 2'b01) begin $error("inside oa forced: %b", u.oa); errs++; end
    release o; release ov; release ow; release os.n; release oa.n;
    #1;
    if (o !== 1'b1 || ov !== 2'b01 || ow !== 2'b01 || os !== 2'b01 || oa !== 2'b01) begin
      $error("outside after release: %b %b %b %b %b", o, ov, ow, os, oa); errs++;
    end
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
