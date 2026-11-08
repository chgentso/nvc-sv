// Expected values: IEEE 1800-2017 6.11/7.2.1 (a packed struct with any 4-state member is a 4-state vector as a
// whole; an all-2-state packed struct converts x/z to 0), 6.8 (2-state variables take 0 for x/z), 10.9.1
// ('{default:1}), 11.4.12 (concatenation)
typedef struct packed { bit [3:0] a; bit b; } s_t;
typedef struct packed { logic [1:0] a; bit b; } m_t;
module svtwostate1;
  wire [4:0] w = 5'bzzzzz;
  wire [4:0] w2 = 5'bxzxzx;
  bit [4:0] x;
  s_t s;
  m_t m;
  s_t [1:0] a2;
  bit [9:0] x10;
  s_t p;
  int errs;
  assign x = w;
  assign s = w2;
  assign m = w2;
  assign a2 = {w2, w};
  assign x10 = {w2, w};
  initial begin
    p = '{default:1};
    if (p !== 5'b00011) begin $error("p %b", p); errs++; end
  end
  initial begin
    #1;
    if (x !== 5'b00000) begin $error("x %b", x); errs++; end
    if (s !== 5'b00000) begin $error("s %b", s); errs++; end
    if (m !== 3'bxzx) begin $error("m %b", m); errs++; end
    if (a2 !== 10'b0) begin $error("a2 %b", a2); errs++; end
    if (x10 !== 10'b0) begin $error("x10 %b", x10); errs++; end
    if (1'bz !== 1'bz) begin $error("1'bz"); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
