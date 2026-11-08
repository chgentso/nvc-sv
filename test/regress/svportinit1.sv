// Edge and change events caused at time zero by the initial value of the
// net that a module/interface port is connected to (variables with
// initialisers, literals, constants, x, bit-selects, 2-state variables).
// Expected values: IEEE 1800-2017 6.8 (a variable initialiser causes no
// event), 23.3.3 (a port is a net driven from the connected expression, whose
// initial value is z), 9.4.2 (event control: edge definitions - negedge is
// 1->0/x/z, x->0, z->0; posedge is 0->1/x/z, x->1, z->1; any value change
// triggers @(v)), 4.9 (events at time zero race with the start of the
// always blocks, so they may or may not be seen).
// For each instance the register bits that some time-zero event could set are
// given by a mask ("may"); bits outside the mask must be 0.  A bit is only
// required to be 1 when it was set by an event after time zero.
module sub(input c, input [1:0] v, output reg [3:0] r);
  initial r = 0;
  always @(posedge c) r[0] <= 1;
  always @(negedge c) r[1] <= 1;
  always @(v) r[2] <= 1;
  always @(posedge v[1]) r[3] <= 1;
endmodule

module subv(input logic c, output reg [1:0] r);
  initial r = 0;
  always @(posedge c) r[0] <= 1;
  always @(negedge c) r[1] <= 1;
endmodule

interface ifc(input c);
  logic [1:0] r;
  initial r = 0;
  always @(posedge c) r[0] <= 1;
  always @(negedge c) r[1] <= 1;
endinterface

module svportinit1;
  reg c1 = 1, c2 = 0, c4, c7 = 1'bx, c9 = 1;
  bit c3;
  reg [1:0] v5 = 2'b10;
  logic [1:0] v6 = 2'b01;
  integer i8 = 1;
  wire [3:0] r1, r2, r3, r4, r5, r6, r7, r8;
  wire [1:0] r9;
  reg loc = 1;
  reg [1:0] lr;
  int errors = 0;
  sub s1(c1, 2'b00, r1);
  sub s2(c2, 2'b00, r2);
  sub s3(c3, 2'b00, r3);
  sub s4(c4, 2'b00, r4);
  sub s5(1'b0, v5, r5);
  sub s6(v6[0], v6, r6);
  sub s7(c7, 2'b00, r7);
  sub s8(i8[0], 2'b00, r8);
  subv s9(c9, r9);
  ifc i1(c1);
  initial lr = 0;
  always @(posedge loc) lr[0] <= 1;

  // r = bits of the register, may/must = masks of bits that can / have to be 1
  task automatic chk(string name, logic [3:0] r, logic [3:0] may, logic [3:0] must);
    if (^r === 1'bx || (r & ~may) !== 4'b0 || (r & must) !== must) begin
      $error("%s = %b: allowed ones %b, required ones %b", name, r, may, must);
      errors++;
    end
  endtask

  initial begin
    #1;
    // time-zero events that could set bits (see header)
    chk("r1", r1, 4'b0101, 4'b0000);   // c=1: posedge; v: change
    chk("r2", r2, 4'b0110, 4'b0000);   // c=0: negedge; v: change
    chk("r3", r3, 4'b0110, 4'b0000);   // c=0 (bit): negedge; v: change
    chk("r4", r4, 4'b0100, 4'b0000);   // c=x: z->x is no edge; v: change
    chk("r5", r5, 4'b1110, 4'b0000);   // c=0: negedge; v=10: change, posedge v[1]
    chk("r6", r6, 4'b0101, 4'b0000);   // c=1: posedge; v=01: change, no posedge v[1]
    chk("r7", r7, 4'b0100, 4'b0000);   // c=x: no edge; v: change
    chk("r8", r8, 4'b0101, 4'b0000);   // c=1: posedge; v: change
    chk("r9", {2'b00, r9}, 4'b0001, 4'b0000);
    chk("i1.r", {2'b00, i1.r}, 4'b0001, 4'b0000);
    // a variable with an initialiser makes no event at all
    if (lr !== 2'b00) begin $error("lr = %b, expected 00", lr); errors++; end
    c1 = 0;
    c3 = 1;
    #1;
    // c1 falls: negedge sets bit 1 of s1; c3 rises: posedge sets bit 0 of s3
    chk("r1 later", r1, 4'b0111, 4'b0010);
    chk("r3 later", r3, 4'b0111, 4'b0001);
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
