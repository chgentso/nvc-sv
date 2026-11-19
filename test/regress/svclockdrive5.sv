// A default output skew given as an edge: outputs are driven at the next
// negedge of the clock; an explicit skew on an item overrides the default.
// Expected values: IEEE 1800-2017 14.3 (clocking_skew ::= edge_identifier
// [ delay_control ] | delay_control; default skews).
`timescale 1ns/1ns
module svclockdrive5;
  logic clk = 0;
  always #5 clk = ~clk;             // posedge at 5, 15; negedge at 10, 20
  wire n;
  logic v, w;
  clocking cb @(posedge clk);
    default output negedge;
    output n;
    output v;
    output #1 w;
  endclocking
  int errors = 0;

  task check(string what, logic got, logic exp);
    if (got !== exp) begin
      $error("%s: got %b expected %b at %t", what, got, exp, $time);
      errors++;
    end
  endtask

  initial begin
    cb.n <= 1;  cb.v <= 1;  cb.w <= 1;
    @(cb);                           // 5
    cb.n <= 0;
  end

  initial begin
    #5.5;  check("n@5.5", n, 1'bz); check("v@5.5", v, 1'bx);
           check("w@5.5", w, 1'bx);
    #1;    check("w@6.5", w, 1'b1);
    #3.5;  check("n@10.5", n, 1'b0); check("v@10.5", v, 1'b1);
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
