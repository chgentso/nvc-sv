// An always_comb with no implicit sensitivities runs once at time zero
module vlogalwayscomb1;
  localparam int L = 1 ? 1 : 2;
  always_comb begin
    logic u;
    u = L'(1'b1);
    $display("comb %0t", $time);
  end
  always_latch begin
    logic w;
    w = 1'b1;
    $display("latch %0t", $time);
  end
endmodule
