// Unknown bits in wide variables, class properties and packed struct members; part-select and
// member writes (including truncation of a wider value) must keep the untouched bits.
// Expected values: IEEE 1800-2017 6.8 (4-state variables and class properties start at x), 11.5.1
// (part-select writes touch only the selected bits), 11.8.2 (a wider value is truncated to the
// target width), 5.7.1 ('x fill), 10.9.1 ('0 assignment pattern), 7.2.1 (packed struct).
typedef struct packed {logic[61:0] v; logic[4:0] ins; logic[3:0] tms; logic r;} ps_t;

class C;
  logic [71:0] w;
  logic [64:0] w3;
  ps_t p;
  ps_t q [2];
endclass

module svwidex1;
  C c;
  logic [63:0] k;
  int errors = 0;
  localparam logic [71:0] AX = {72{1'bx}};

  function automatic logic [71:0] f(int n);
    logic [71:0] loc;
    if (n > 0) return loc;
    return 72'bx;
  endfunction

  task automatic chk(logic [71:0] got, logic [71:0] exp, string what);
    if (got !== exp) begin $error("%s: got %b exp %b", what, got, exp); errors++; end
  endtask

  initial begin
    automatic logic [71:0] w;
    automatic logic [71:0] z;
    c = new;
    k = 64'hFFFF_FFFF_FFFF_FFFF;
    chk(c.w, AX, "c.w");
    if (c.w3 !== {65{1'bx}}) begin $error("c.w3 %b", c.w3); errors++; end
    chk(c.p, AX, "c.p");
    chk(c.q[1], AX, "c.q[1]");
    chk(f(1), AX, "f(1)");
    chk(f(0), AX, "f(0)");
    c.p = '0;
    c.p.v = 64'hFFFF_FFFF_FFFF_FFFF;
    chk(c.p, {{62{1'b1}}, 10'b0}, "p.v truncated");
    c.p = 'x;
    c.p.v = k;
    chk(c.p, {{62{1'b1}}, {10{1'bx}}}, "p.v = k");
    c.p = 'x;
    c.p.v = 62'h3FFF_FFFF_FFFF_FFFF;
    chk(c.p, {{62{1'b1}}, {10{1'bx}}}, "p.v = 62 ones");
    c.w = 'x;
    c.w[71:10] = k;
    chk(c.w, {{62{1'b1}}, {10{1'bx}}}, "c.w[71:10] = k");
    w = 'x;
    w[71:10] = k;
    chk(w, {{62{1'b1}}, {10{1'bx}}}, "w[71:10] = k");
    w = 'x;
    w[71:10] = 62'h1;
    chk(w, {{61{1'b0}}, 1'b1, {10{1'bx}}}, "w[71:10] = 1");
    z = 'x;
    z[71:64] = 8'h0;
    chk(z, {8'h00, {64{1'bx}}}, "z[71:64]");
    z = 'x;
    z[70:3] = 68'h0;
    chk(z, {1'bx, 68'h0, 3'bxxx}, "z[70:3]");
    z = 'x;
    z[65:2] = 64'h8000_0000_0000_0001;
    chk(z, {{6{1'bx}}, 1'b1, {62{1'b0}}, 1'b1, 2'bxx}, "z[65:2]");
    if (errors == 0) $display("PASSED");
  end
endmodule
