// Compound assignment operators with a concatenation as the left-hand side
// Expected values: IEEE 1800-2017 11.4.1 (assignment operators: a op= b behaves as
// a = a op b with a evaluated once), 11.4.12 (concatenation as an lvalue, leftmost
// element holds the most significant bits), 11.4.8 (bitwise operators on x/z), 11.6
module svconcatopassign1;
  typedef struct packed { logic [3:0] a; logic [3:0] b; } s_t;
  bit [7:0] x = 8'h0f;
  bit [3:0] y = 4'h3;
  logic [3:0] l = 4'h6;
  s_t s = '{a: 4'h5, b: 4'ha};
  bit [11:0] mask = 12'h0f0;
  int errs = 0;
  initial begin
    // {x,s.a} = 12'h0f5 ^ 12'h0f0 = 12'h005
    {x, s.a} ^= mask;
    if (x !== 8'h00 || s.a !== 4'h5 || s.b !== 4'ha) begin $error("xor: %h %h %h", x, s.a, s.b); errs++; end
    // {y,l} = 8'h36 + 8'h11 = 8'h47
    {y, l} += 8'h11;
    if (y !== 4'h4 || l !== 4'h7) begin $error("add: %h %h", y, l); errs++; end
    // {x,l} = 12'h007 ^ 12'hf0f = 12'hf08
    {x, l} ^= 12'hf0f;
    if (x !== 8'hf0 || l !== 4'h8) begin $error("xor2: %h %h", x, l); errs++; end
    // x or 0 is x, 1 or anything is 1, 0 or 1 is 1
    l = 4'bx1x0;
    {y, l} |= 8'h01;
    if (y !== 4'h4 || l !== 4'bx1x1) begin $error("or: %h %b", y, l); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
