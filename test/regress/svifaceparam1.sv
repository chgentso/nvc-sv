// Self-checking. IEEE 1800-2017 25.3: a parameter of an interface instance
// can be used in a constant expression of the module it is connected to,
// including the size of a variable declared with it.
interface svifaceparam1_if #(parameter int W = 4) ();
  localparam int V = W * 2;
  localparam int S = 3;
endinterface

module svifaceparam1_sub(svifaceparam1_if i, output bit [7:0] bits);
  localparam int W = i.W;
  logic [W-1:0] x;
  typedef struct packed { logic [i.V-1:0] a; logic [i.W-1:0] b; } s_t;
  s_t y;
  initial begin
    x = '1;
    y.a = '1;
    y.b = 0;
    #1;
    if ($bits(x) !== 8 || x !== 8'hff)
      $error("FAIL x %0d %h", $bits(x), x);
    else if ($bits(y) !== 24 || y !== 24'hffff00)
      $error("FAIL y %0d %h", $bits(y), y);
    else
      bits = 8'd1;
  end
endmodule

module svifaceparam1;
  svifaceparam1_if #(8) i0();
  localparam int LW = i0.W;
  logic [LW-1:0] z;
  logic [i0.V-1:0] v;
  bit [7:0] bits;
  svifaceparam1_sub s(i0, bits);
  initial begin
    z = '1;
    v = '1;
    #2;
    if ($bits(z) !== 8 || z !== 8'hff)
      $error("FAIL z %0d %h", $bits(z), z);
    else if ($bits(v) !== 16 || v !== 16'hffff)
      $error("FAIL v %0d %h", $bits(v), v);
    else if (bits !== 8'd1)
      $error("FAIL sub");
    else
      $display("PASSED");
  end
endmodule
