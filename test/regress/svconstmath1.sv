// $bits and constant math functions in constant expressions
// Expected values: IEEE 1800-2017 20.6.2 ($bits), 20.8.2 ($ln, $log10, $sqrt, $pow, $exp,
// $sin, $cos), 11.2.1 (constant expressions), 6.24.1 (int' casts round to nearest), 6.20
typedef enum bit [3:0] {EA, EB} e_t;
typedef struct packed {logic [3:0] a; bit [6:0] b;} s_t;
class P #(parameter type T = int);
  logic [$bits(T)-1:0] mem;
  static function int w(); return $bits(T); endfunction
endclass
package cp;
  parameter int ELEN = 32;
  parameter int VELEN = int'($ln(ELEN)/$ln(2)) - 3;
  parameter int L2 = int'($log10(1000)) + int'($sqrt(16.0)) + int'($pow(2, 3));
  parameter real R = $exp(0.0) + $sin(0.0) + $cos(0.0);
endpackage
module svconstmath1;
  import cp::*;
  logic [$bits(e_t)-1:0] x;
  logic [$bits(s_t)-1:0] y;
  logic [VELEN:0] z;
  P#(e_t) p;
  int errs = 0;
  function automatic void chk(string m, longint got, longint exp);
    if (got !== exp) begin $error("%s: got %0d expected %0d", m, got, exp); errs++; end
  endfunction
  initial begin
    p = new;
    chk("bits x", $bits(x), 4);              // enum base type bit [3:0]
    chk("bits y", $bits(y), 11);             // 4 + 7
    chk("w", P#(s_t)::w(), 11);
    chk("bits mem", $bits(p.mem), 4);
    chk("VELEN", VELEN, 2);                  // log2(32) = 5, minus 3
    chk("bits z", $bits(z), 3);              // VELEN + 1
    chk("L2", L2, 15);                       // 3 + 4 + 8
    chk("R", int'(R), 2);                    // 1 + 0 + 1
    if (errs == 0) $display("PASSED");
  end
endmodule
