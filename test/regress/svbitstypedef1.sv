// $bits of packed-struct typedefs whose widths come from localparams and package parameters (self-checking).
// Expected values: IEEE 1800-2017 20.6.2 ($bits), 7.2.1 (packed struct: members concatenated, first member in
// the most significant bits), 7.2.2 (assignment pattern for a packed struct), 6.24.1 (assignment between a
// packed struct and an integral value), 6.20.2.
// Reference model: a_t = 3+1 = 4; b_t = 2; d_t = 1+8+2 = 11; tp::pk_t = 8; y = 4; DW = 11.
// d = '{r:1, src:8'h64, size:2'd1} -> flat = {1'b1, 8'b01100100, 2'b01} = 11'b10110010001; e = flat restores
// the members.
package tp;
  parameter int SZW = 2;
  parameter int AIW = 8;
  typedef struct packed { logic [AIW-1:0] s; } pk_t;
endpackage
module svbitstypedef1;
  localparam int LW = 3;
  typedef struct packed { logic [LW-1:0] s; logic z; } a_t;
  typedef struct packed { logic [tp::SZW-1:0] s; } b_t;
  typedef struct packed { logic r; logic [tp::AIW-1:0] src; logic [tp::SZW-1:0] size; } d_t;
  localparam int DW = $bits(d_t);
  logic [$bits(a_t)-1:0] y;
  logic [DW-1:0] flat;
  d_t d, e;
  int errs = 0;
  initial begin
    d = '{r: 1'b1, src: 8'h64, size: 2'd1};
    flat = d;
    e = flat;
    if ($bits(a_t) != 4) begin errs++; $error("$bits(a_t) = %0d", $bits(a_t)); end
    if ($bits(b_t) != 2) begin errs++; $error("$bits(b_t) = %0d", $bits(b_t)); end
    if ($bits(d_t) != 11) begin errs++; $error("$bits(d_t) = %0d", $bits(d_t)); end
    if ($bits(tp::pk_t) != 8) begin errs++; $error("$bits(pk_t) = %0d", $bits(tp::pk_t)); end
    if ($bits(y) != 4) begin errs++; $error("$bits(y) = %0d", $bits(y)); end
    if (DW != 11) begin errs++; $error("DW = %0d", DW); end
    if (e.src !== 8'h64 || e.size !== 2'd1 || e.r !== 1'b1) begin errs++; $error("e = %b", e); end
    if (flat !== 11'b10110010001) begin errs++; $error("flat = %b", flat); end
    if (errs == 0) $display("PASSED");
  end
endmodule
