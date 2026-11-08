// $bits of selects of a packed-struct port inside a localparam (self-checking).
// Expected values: IEEE 1800-2017 20.6.2 ($bits of an expression or data type), 7.4.1 (packed array of
// packed array: key[i] is KeyWidth bits), 11.5.1 (bit/part select widths), 7.2.1 (packed struct width),
// 6.20.2 (constant expressions in localparams).
// Reference model: key is [1:0][127:0]: key[0] -> 128, key -> 256, key[1][3:0] -> 4, v -> W = 128.
package kp;
  parameter int Shares = 2;
  parameter int KeyWidth = 128;
  typedef struct packed { logic valid; logic [Shares-1:0][KeyWidth-1:0] key; } k_t;
endpackage
module sub (input kp::k_t ki);
  localparam int W = $bits(ki.key[0]);
  localparam int W3 = $bits(ki.key);
  localparam int W4 = $bits(ki.key[1][3:0]);
  logic [W-1:0] v;
  int bad = 0;
  initial begin
    if (W != 128) begin bad++; $error("W = %0d, expected 128", W); end
    if (W3 != 256) begin bad++; $error("W3 = %0d, expected 256", W3); end
    if (W4 != 4) begin bad++; $error("W4 = %0d, expected 4", W4); end
    if ($bits(v) != 128) begin bad++; $error("$bits(v) = %0d, expected 128", $bits(v)); end
    if ($bits(ki) != 257) begin bad++; $error("$bits(ki) = %0d, expected 257", $bits(ki)); end
  end
endmodule
module svbitselem1;
  kp::k_t a;
  sub t(a);
  initial begin
    #1;
    if (t.bad == 0) $display("PASSED");
    $finish;
  end
endmodule
