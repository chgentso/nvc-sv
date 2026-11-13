// A null statement as the body of an always construct, as emitted by
// register file generators
module svalwaysnull1;
  logic a = 1, b;
  always_comb ;
  always_ff @(posedge a) ;
  always_latch ;
  always @(a) ;
  always_comb begin
    b = a;
  end
  initial begin
    #1;
    if (b === 1'b1) $display("PASSED");
    else $display("FAILED");
  end
endmodule
