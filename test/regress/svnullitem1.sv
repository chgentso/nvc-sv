// Null module items and null generate items (a stray ';') do not change behaviour
// Expected values: IEEE 1800-2017 9.2.2.3 / 9.4.2 (always @(posedge clk) runs once per rising edge),
// 27.4 (generate loop creates 2 instances of the always block)
// clk rises at 5, 15, 25 before the check at 32: n counts 3, m (two always blocks) counts 6.
`timescale 1ns/1ns
module svnullitem1;
  logic clk = 0;
  int n = 0, m = 0;
  always #5 clk = ~clk;
  always @(posedge clk) begin
    n = n + 1;
  end;
  ;
  genvar j;
  generate
    for (j = 0; j < 2; j = j + 1) begin : g
      always @(posedge clk) begin
        m = m + 1;
      end;
    end;
  endgenerate
  initial begin
    #32;
    if (n !== 3) $error("n=%0d expected 3", n);
    else if (m !== 6) $error("m=%0d expected 6", m);
    else $display("PASSED");
    $finish;
  end
endmodule;
