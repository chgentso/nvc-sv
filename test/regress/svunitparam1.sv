// Expected values: IEEE 1800-2017 3.12.1 (compilation-unit scope declarations), 6.20.2 (value parameters),
// 20.8.1 ($clog2), 20.6.2 ($bits), 20.7 ($size)
// XLEN=32, W=64, V=64/4+5=21, LIMIT=100
parameter int XLEN = 32;
parameter int W = XLEN * 2;
localparam int V = W / 4 + $clog2(XLEN);
parameter int LIMIT = (XLEN > 16) ? 100 : 50;
module m;
  logic [W-1:0] x;
  logic [V-1:0] y;
  bit [XLEN*4-1:0] z;
  int arr[LIMIT];
  initial begin
    if ($bits(x) !== 64 || $bits(y) !== 21 || $bits(z) !== 128 || $size(arr) !== 100)
      $error("got %0d %0d %0d %0d, expected 64 21 128 100", $bits(x), $bits(y), $bits(z), $size(arr));
    else
      $display("PASSED");
  end
endmodule
module svunitparam1;
  m i();
endmodule
