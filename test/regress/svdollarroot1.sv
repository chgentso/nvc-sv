// $root hierarchical references from a continuous assignment and a procedural read.
// Expected values: IEEE 1800-2017 23.3.1 / 23.9 ($root names the root of the instance
// hierarchy), 6.8 (variable initializers take effect before time 0 processes),
// 10.3 (continuous assignment), 21.2 (%d).
module sub;
  logic [3:0] q = 5;
  struct packed { logic [3:0] a; } s = 4'h9;
endmodule
module chk;
  wire [3:0] w;
  logic [3:0] v;
  assign w = $root.svdollarroot1.dut.q;
  assign v = $root.svdollarroot1.dut.s.a;
  initial begin
    #1;
    if (w !== 4'd5) $error("w = %0d, expected 5", w);
    else if (v !== 4'd9) $error("v = %0d, expected 9", v);
    else if ($root.svdollarroot1.dut.q !== 4'd5) $error("procedural $root read");
    else $display("PASSED");
    $finish;
  end
endmodule
module svdollarroot1;
  sub dut();
  chk c();
endmodule
