// Data types as arguments of $bits and the array query functions
// (IEEE 1800-2017 20.6.2, 20.7)
module svtypequery1;
  typedef enum logic [2:0] { A, B } e_t;
  static int s1 = $size(logic [7:0]);
  localparam int L = $bits(struct packed { logic a; logic [2:0] b; });
  initial begin
    $display("%0d %0d %0d %0d", s1, L, $bits(real), $bits(shortreal));
    $display("%0d %0d %0d", $left(bit [3:1]), $right(logic [0:5]), $dimensions(int));
    $display("%0d %0d %0d", $high(logic [4:2][1:0]), $increment(bit [0:3]), $unpacked_dimensions(byte));
    $display("%0d %0d", $bits(enum logic [4:0] {X, Y}), $bits(union packed { logic [3:0] a; bit [3:0] b; }));
    begin real r; shortreal sr; $display("%0d %0d %0d", $bits(int), $bits(r), $bits(sr)); end
  end
endmodule
